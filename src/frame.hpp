#pragma once

#include "common.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace cv {

struct FrameView {
  std::span<const std::byte> bytes;
  PixelFormat format = PixelFormat::Unknown;
  Size size;
  uint64_t sequence = 0;
  Clock::time_point captured_at{};
};

struct RgbaFrame {
  Size size;
  std::vector<std::byte> pixels;
  uint64_t sequence = 0;
};

void convert_yuyv_to_rgba(FrameView src, RgbaFrame& dst);
void convert_nv12_to_rgba(FrameView src, RgbaFrame& dst);
void convert_rgba_to_yuyv(const RgbaFrame& src, std::vector<std::byte>& dst);
void convert_rgba_to_nv12(const RgbaFrame& src, std::vector<std::byte>& dst);

} // namespace cv
