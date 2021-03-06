#include "detection.h"

namespace lucrezio_semantic_perception{
  Detection::Detection(const std::string &type_,
                       const Eigen::Vector2i &top_left_,
                       const Eigen::Vector2i &bottom_right_,
                       const std::vector<Eigen::Vector2i> &pixels_):
    _type(type_),
    _top_left(top_left_),
    _bottom_right(bottom_right_),
    _pixels(pixels_){}

}
