#pragma once

#include "frame.hpp"

#include <string>
#include <vector>

namespace cv {

struct V4l2OutputConfig {
  std::string device;
  Size size;
  uint32_t fps = 60;
  std::string format = "yuyv";
};

class V4l2Output {
public:
  explicit V4l2Output(V4l2OutputConfig config);
  ~V4l2Output();

  V4l2Output(const V4l2Output&) = delete;
  V4l2Output& operator=(const V4l2Output&) = delete;

  void write_frame(const RgbaFrame& frame);

private:
  void open_device();
  void configure_format();
  void close_device();

  V4l2OutputConfig config_;
  int fd_ = -1;
  std::vector<std::byte> buffer_;
};

} // namespace cv
