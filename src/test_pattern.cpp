#include "test_pattern.hpp"

#include <cstdint>

namespace cv {

TestPattern::TestPattern(Size size) : size_(size) {
  frame_.size = size_;
  frame_.pixels.resize(static_cast<size_t>(size_.width) * size_.height * 4);
}

RgbaFrame TestPattern::next() {
  ++sequence_;
  frame_.sequence = sequence_;
  auto* out = reinterpret_cast<uint8_t*>(frame_.pixels.data());
  for (uint32_t y = 0; y < size_.height; ++y) {
    for (uint32_t x = 0; x < size_.width; ++x) {
      const size_t i = (static_cast<size_t>(y) * size_.width + x) * 4;
      out[i + 0] = static_cast<uint8_t>((x + sequence_ * 4) % 256);
      out[i + 1] = static_cast<uint8_t>((y + sequence_ * 2) % 256);
      out[i + 2] = static_cast<uint8_t>(((x / 32 + y / 32 + sequence_ / 8) % 2) ? 220 : 40);
      out[i + 3] = 255;
    }
  }
  return frame_;
}

} // namespace cv
