#pragma once

#include "common.hpp"

#include <string>
#include <vector>

namespace cv {

struct FrameInterval {
  uint32_t numerator = 1;
  uint32_t denominator = 0;
};

struct FrameSizeInfo {
  Size size;
  std::vector<FrameInterval> intervals;
};

struct FormatInfo {
  uint32_t fourcc = 0;
  std::string description;
  std::vector<FrameSizeInfo> sizes;
};

struct VideoDeviceInfo {
  std::string path;
  std::string driver;
  std::string card;
  std::string bus;
  std::vector<FormatInfo> formats;
};

std::vector<VideoDeviceInfo> list_video_devices();
void print_video_devices(const std::vector<VideoDeviceInfo>& devices);

} // namespace cv
