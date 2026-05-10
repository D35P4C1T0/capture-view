#pragma once

#include "frame.hpp"

namespace cv {

class TestPattern {
public:
  explicit TestPattern(Size size);
  RgbaFrame next();

private:
  Size size_;
  uint64_t sequence_ = 0;
  RgbaFrame frame_;
};

} // namespace cv
