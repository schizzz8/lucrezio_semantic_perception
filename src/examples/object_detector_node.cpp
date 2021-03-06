#include <iostream>
#include <ros/ros.h>
#include <sensor_msgs/CameraInfo.h>
#include <Eigen/Core>
#include <image_transport/image_transport.h>
#include <cv_bridge/cv_bridge.h>
#include <sensor_msgs/Image.h>
#include <lucrezio_simulation_environments/LogicalImage.h>

#include "tf/tf.h"
#include "tf/transform_listener.h"
#include "tf/transform_datatypes.h"

#include <message_filters/subscriber.h>
#include <message_filters/synchronizer.h>
#include <message_filters/sync_policies/approximate_time.h>

#include <pcl_ros/point_cloud.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>

#include <lucrezio_semantic_perception/ImageBoundingBoxesArray.h>


typedef pcl::PointCloud<pcl::PointXYZ> PointCloudType;
typedef std::vector<lucrezio_simulation_environments::Model> Models;
typedef std::pair<Eigen::Vector3f,Eigen::Vector3f> BoundingBox3D;
typedef std::vector<BoundingBox3D> BoundingBoxes3D;

class Detection{
public:
  Detection(const std::string& type_="",
            const Eigen::Vector2i& top_left_ = Eigen::Vector2i(10000,10000),
            const Eigen::Vector2i& bottom_right_ = Eigen::Vector2i(-10000,-10000),
            const std::vector<Eigen::Vector2i>& pixels_ = std::vector<Eigen::Vector2i>()):
    _type(type_),
    _top_left(top_left_),
    _bottom_right(bottom_right_),
    _pixels(pixels_){}

  inline const std::string& type() {return _type;}
  inline const Eigen::Vector2i& topLeft() {return _top_left;}
  inline const Eigen::Vector2i& bottomRight() {return _bottom_right;}
  inline const std::vector<Eigen::Vector2i>& pixels() {return _pixels;}

  std::string _type;
  Eigen::Vector2i _top_left;
  Eigen::Vector2i _bottom_right;
  std::vector<Eigen::Vector2i> _pixels;
};

typedef std::vector<Detection> Detections;


class ObjectDetector{
public:
  ObjectDetector(ros::NodeHandle nh_):
    _nh(nh_),
    _logical_image_sub(_nh,"/gazebo/logical_camera_image",1),
    _depth_cloud_sub(_nh,"/camera/depth/points",1),
    _rgb_image_sub(_nh,"/camera/rgb/image_raw", 1),
    _synchronizer(FilterSyncPolicy(10),_logical_image_sub,_depth_cloud_sub,_rgb_image_sub),
    _it(_nh){

    _got_info = false;
    _camera_info_sub = _nh.subscribe("/camera/depth/camera_info",
                                     1000,
                                     &ObjectDetector::cameraInfoCallback,
                                     this);

    _synchronizer.registerCallback(boost::bind(&ObjectDetector::filterCallback, this, _1, _2, _3));

    _image_bounding_boxes_pub = _nh.advertise<lucrezio_semantic_perception::ImageBoundingBoxesArray>("/image_bounding_boxes", 1);
    _label_image_pub = _it.advertise("/camera/rgb/label_image", 1);

    ROS_INFO("Starting detection simulator node!");
  }

  void cameraInfoCallback(const sensor_msgs::CameraInfo::ConstPtr& camera_info_msg){
    sensor_msgs::CameraInfo camerainfo;
    camerainfo.K = camera_info_msg->K;

    ROS_INFO("Got camera info!");
    _K(0,0) = camerainfo.K.c_array()[0];
    _K(0,1) = camerainfo.K.c_array()[1];
    _K(0,2) = camerainfo.K.c_array()[2];
    _K(1,0) = camerainfo.K.c_array()[3];
    _K(1,1) = camerainfo.K.c_array()[4];
    _K(1,2) = camerainfo.K.c_array()[5];
    _K(2,0) = camerainfo.K.c_array()[6];
    _K(2,1) = camerainfo.K.c_array()[7];
    _K(2,2) = camerainfo.K.c_array()[8];
    std::cerr << _K << std::endl;

    _invK = _K.inverse();
    _got_info = true;
    _camera_info_sub.shutdown();
  }

  void filterCallback(const lucrezio_simulation_environments::LogicalImage::ConstPtr& logical_image_msg,
                      const PointCloudType::ConstPtr& depth_cloud_msg,
                      const sensor_msgs::Image::ConstPtr& rgb_image_msg){

    if(_got_info && !logical_image_msg->models.empty()){

      ROS_INFO("--------------------------");
      ROS_INFO("Executing filter callback!");
      ROS_INFO("--------------------------");
      std::cerr << std::endl;

      //Save timestamp
      _last_timestamp = logical_image_msg->header.stamp;

      //Extract rgb and depth image from ROS messages
      cv_bridge::CvImageConstPtr rgb_cv_ptr;
      try{
        rgb_cv_ptr = cv_bridge::toCvShare(rgb_image_msg);
      } catch (cv_bridge::Exception& e) {
        ROS_ERROR("cv_bridge exception: %s", e.what());
        return;
      }
      _rgb_image = rgb_cv_ptr->image.clone();

      //Listen to depth camera pose
      tf::StampedTransform depth_camera_pose;
      try {
        _listener.waitForTransform("map",
                                   "camera_depth_optical_frame",
                                   ros::Time(0),
                                   ros::Duration(3));
        _listener.lookupTransform("map",
                                  "camera_depth_optical_frame",
                                  ros::Time(0),
                                  depth_camera_pose);
      }
      catch(tf::TransformException ex) {
        ROS_ERROR("%s", ex.what());
      }
      _depth_camera_transform = tfTransform2eigen(depth_camera_pose);
      _inverse_depth_camera_transform = _depth_camera_transform.inverse();

      //detect objects
      _depth_cloud = depth_cloud_msg;
      this->detectObjects(logical_image_msg);

      //publish image bounding boxes
      publishImageBoundingBoxes();
      sensor_msgs::ImagePtr label_image_msg = cv_bridge::CvImage(std_msgs::Header(),
                                                                 "bgr8",
                                                                 _rgb_image).toImageMsg();
      _label_image_pub.publish(label_image_msg);

      //            std::cerr << ".";
      //            _logical_image_sub.unsubscribe();
    }
  }

protected:
  ros::NodeHandle _nh;

  ros::Subscriber _camera_info_sub;
  Eigen::Matrix3f _K,_invK;
  bool _got_info;

  tf::TransformListener _listener;
  cv::Mat _rgb_image;
  Eigen::Isometry3f _depth_camera_transform,_inverse_depth_camera_transform;
  PointCloudType::ConstPtr _depth_cloud;

  message_filters::Subscriber<lucrezio_simulation_environments::LogicalImage> _logical_image_sub;
  message_filters::Subscriber<PointCloudType> _depth_cloud_sub;
  message_filters::Subscriber<sensor_msgs::Image> _rgb_image_sub;
  typedef message_filters::sync_policies::ApproximateTime<lucrezio_simulation_environments::LogicalImage,
  PointCloudType,
  sensor_msgs::Image> FilterSyncPolicy;
  message_filters::Synchronizer<FilterSyncPolicy> _synchronizer;

  ros::Time _last_timestamp;
  Detections _detections;
  ros::Publisher _image_bounding_boxes_pub;

  image_transport::ImageTransport _it;
  image_transport::Publisher _label_image_pub;

private:

  Eigen::Isometry3f tfTransform2eigen(const tf::Transform& p){
    Eigen::Isometry3f iso;
    iso.translation().x()=p.getOrigin().x();
    iso.translation().y()=p.getOrigin().y();
    iso.translation().z()=p.getOrigin().z();
    Eigen::Quaternionf q;
    tf::Quaternion tq = p.getRotation();
    q.x()= tq.x();
    q.y()= tq.y();
    q.z()= tq.z();
    q.w()= tq.w();
    iso.linear()=q.toRotationMatrix();
    return iso;
  }

  tf::Transform eigen2tfTransform(const Eigen::Isometry3f& T){
    Eigen::Quaternionf q(T.linear());
    Eigen::Vector3f t=T.translation();
    tf::Transform tft;
    tft.setOrigin(tf::Vector3(t.x(), t.y(), t.z()));
    tft.setRotation(tf::Quaternion(q.x(), q.y(), q.z(), q.w()));
    return tft;
  }

  void computeWorldBoundingBoxes(BoundingBoxes3D &bounding_boxes,
                                 const Eigen::Isometry3f &transform,
                                 const Models &models){
    for(int i=0; i<models.size(); ++i){
      const lucrezio_simulation_environments::Model &model = models[i];

      tf::Transform model_pose;
      tf::poseMsgToTF(model.pose,model_pose);

      std::vector<Eigen::Vector3f> points;
      points.push_back(transform*tfTransform2eigen(model_pose)*Eigen::Vector3f(model.min.x,model.min.y,model.min.z));
      points.push_back(transform*tfTransform2eigen(model_pose)*Eigen::Vector3f(model.max.x,model.max.y,model.max.z));

      float x_min=100000,x_max=-100000,y_min=100000,y_max=-100000,z_min=100000,z_max=-100000;
      for(int i=0; i < 2; ++i){
        if(points[i].x()<x_min)
          x_min = points[i].x();
        if(points[i].x()>x_max)
          x_max = points[i].x();
        if(points[i].y()<y_min)
          y_min = points[i].y();
        if(points[i].y()>y_max)
          y_max = points[i].y();
        if(points[i].z()<z_min)
          z_min = points[i].z();
        if(points[i].z()>z_max)
          z_max = points[i].z();
      }
      bounding_boxes[i] = std::make_pair(Eigen::Vector3f(x_min,y_min,z_min),Eigen::Vector3f(x_max,y_max,z_max));
      _detections[i]._type = model.type;
    }
  }


  inline bool inRange(const pcl::PointXYZ &point, const BoundingBox3D &bounding_box){
    return (point.x >= bounding_box.first.x() && point.x < bounding_box.second.x() &&
            point.y >= bounding_box.first.y() && point.y < bounding_box.second.y() &&
            point.z >= bounding_box.first.z() && point.z < bounding_box.second.z());
  }

  void computeImageBoundingBoxes(const BoundingBoxes3D &bounding_boxes){
    for(int i=0; i < _depth_cloud->size(); ++i){
      for(int j=0; j < bounding_boxes.size(); ++j){
        if(inRange(_depth_cloud->points[i],bounding_boxes[j])){
          Eigen::Vector3f image_point = _K*Eigen::Vector3f(_depth_cloud->points[i].x,
                                                           _depth_cloud->points[i].y,
                                                           _depth_cloud->points[i].z);

          const float& z=image_point.z();
          image_point.head<2>()/=z;
          int r = image_point.x();
          int c = image_point.y();
          int &r_min = _detections[j]._top_left.x();
          int &c_min = _detections[j]._top_left.y();
          int &r_max = _detections[j]._bottom_right.x();
          int &c_max = _detections[j]._bottom_right.y();

          if(r < r_min)
            r_min = r;
          if(r > r_max)
            r_max = r;

          if(c < c_min)
            c_min = c;
          if(c > c_max)
            c_max = c;

          _detections[j]._pixels.push_back(Eigen::Vector2i(c,r));
          _rgb_image.at<cv::Vec3b>(c,r) = cv::Vec3b(255,0,0);

          break;
        }
      }
    }
  }

  void detectObjects(const lucrezio_simulation_environments::LogicalImage::ConstPtr& logical_image_msg){

    int num_models = logical_image_msg->models.size();
    std::cerr << "num_models: " << num_models << std::endl;
    _detections.clear();
    _detections.resize(num_models);

    //Compute world bounding boxes
    double cv_wbb_time = (double)cv::getTickCount();
    BoundingBoxes3D bounding_boxes(num_models);
    tf::StampedTransform logical_camera_pose;
    tf::poseMsgToTF(logical_image_msg->pose,logical_camera_pose);
    computeWorldBoundingBoxes(bounding_boxes,
                              _inverse_depth_camera_transform*tfTransform2eigen(logical_camera_pose),
                              logical_image_msg->models);
    ROS_INFO("Computing WBB took: %f",((double)cv::getTickCount() - cv_wbb_time)/cv::getTickFrequency());

    //Compute image bounding boxes
    double cv_ibb_time = (double)cv::getTickCount();
    computeImageBoundingBoxes(bounding_boxes);
    ROS_INFO("Computing IBB took: %f",((double)cv::getTickCount() - cv_ibb_time)/cv::getTickFrequency());
  }

  void publishImageBoundingBoxes(){
    //        std::cerr << "Publishing detections" << std::endl;
    lucrezio_semantic_perception::ImageBoundingBoxesArray image_bounding_boxes;
    image_bounding_boxes.header.frame_id = "camera_depth_optical_frame";
    image_bounding_boxes.header.stamp = _last_timestamp;
    lucrezio_semantic_perception::ImageBoundingBox image_bounding_box;
    for(int i=0; i < _detections.size(); ++i){
      //            std::cerr << "#" << i+1 << std::endl;
      image_bounding_box.type = _detections[i].type();
      image_bounding_box.top_left.r = _detections[i].topLeft().x();
      image_bounding_box.top_left.c = _detections[i].topLeft().y();
      image_bounding_box.bottom_right.r = _detections[i].bottomRight().x();
      image_bounding_box.bottom_right.c = _detections[i].bottomRight().y();
      lucrezio_semantic_perception::Pixel pixel;
      for(int j=0; j < _detections[i]._pixels.size(); ++j){
        pixel.r = _detections[i]._pixels[j].x();
        pixel.c = _detections[i]._pixels[j].y();
        image_bounding_box.pixels.push_back(pixel);
      }
      image_bounding_boxes.image_bounding_boxes.push_back(image_bounding_box);
    }
    _image_bounding_boxes_pub.publish(image_bounding_boxes);
  }
};

int main(int argc, char** argv){
  ros::init(argc, argv, "detection_simulator");
  ros::NodeHandle nh;
  ObjectDetector simulator(nh);

  //ros::spin();

  ros::Rate loop_rate(1);
  while(ros::ok()){
    ros::spinOnce();
    loop_rate.sleep();
  }


  return 0;
}
