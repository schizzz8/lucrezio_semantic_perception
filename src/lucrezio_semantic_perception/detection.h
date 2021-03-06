#pragma once

#include <string>
#include <vector>
#include <Eigen/Core>

namespace lucrezio_semantic_perception{

  class Detection;
  typedef std::vector<Detection> DetectionVector;

  class Detection{
  public:
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW;
    Detection(const std::string &type_="",
              const Eigen::Vector2i &top_left_ = Eigen::Vector2i(10000,10000),
              const Eigen::Vector2i &bottom_right_ = Eigen::Vector2i(-10000,-10000),
              const std::vector<Eigen::Vector2i> &pixels_ = std::vector<Eigen::Vector2i>());

    inline const std::string &type() const {return _type;}
    inline std::string &type() {return _type;}
    inline const Eigen::Vector2i &topLeft() const {return _top_left;}
    inline Eigen::Vector2i &topLeft() {return _top_left;}
    inline const Eigen::Vector2i &bottomRight() const {return _bottom_right;}
    inline Eigen::Vector2i &bottomRight() {return _bottom_right;}
    inline const std::vector<Eigen::Vector2i> &pixels() const {return _pixels;}
    inline std::vector<Eigen::Vector2i> &pixels() {return _pixels;}

  private:
    std::string _type;
    Eigen::Vector2i _top_left;
    Eigen::Vector2i _bottom_right;
    std::vector<Eigen::Vector2i> _pixels;
  };

  }

