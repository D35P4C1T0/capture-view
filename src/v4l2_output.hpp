#pragma once

#include "frame.hpp"
#include "v4l2_util.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace cv {

struct V4l2OutputConfig {
  std::string device;
  Size size;
  uint32_t fps = 60;
  std::string format = "yuyv";
};

struct V4l2OutputStats {
  uint64_t frames = 0;
  uint64_t dropped_writes = 0;
};

class V4l2Output {
public:
  explicit V4l2Output(V4l2OutputConfig config);
  ~V4l2Output();

  V4l2Output(const V4l2Output&) = delete;
  V4l2Output& operator=(const V4l2Output&) = delete;

  void write_frame(const RgbaFrame& frame);
  void write_frame(FrameView frame);
  [[nodiscard]] V4l2OutputStats stats() const { return stats_; }

private:
  void open_device();
  void configure_format();
  void close_device();

  V4l2OutputConfig config_;
  UniqueFd fd_;
  std::vector<std::byte> buffer_;
  RgbaFrame rgba_;
  V4l2OutputStats stats_;
};

} // namespace cv
