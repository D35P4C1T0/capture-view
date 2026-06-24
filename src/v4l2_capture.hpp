#pragma once

#include "frame.hpp"
#include "v4l2_util.hpp"

#include <optional>
#include <string>
#include <vector>

namespace cv {

struct CaptureConfig {
  std::string device;
  Size size;
  uint32_t fps = 60;
  PixelFormat format = PixelFormat::Mjpeg;
  uint32_t buffer_count = 3;
};

struct CaptureStats {
  uint64_t frames = 0;
  uint64_t dropped = 0;
};

class V4l2Capture {
public:
  explicit V4l2Capture(CaptureConfig config);
  ~V4l2Capture();

  V4l2Capture(const V4l2Capture&) = delete;
  V4l2Capture& operator=(const V4l2Capture&) = delete;

  void start();
  void stop();
  void restart();

  [[nodiscard]] std::optional<FrameView> poll_newest(int timeout_ms);
  [[nodiscard]] CaptureStats stats() const { return stats_; }
  [[nodiscard]] Size size() const { return size_; }
  [[nodiscard]] PixelFormat format() const { return format_; }

private:
  struct Buffer {
    void* start = nullptr;
    size_t length = 0;
  };

  void open_device();
  void configure_format();
  void create_buffers();
  void queue_all_buffers();
  void destroy_buffers();
  void close_device();
  void requeue(uint32_t index);

  CaptureConfig config_;
  UniqueFd fd_;
  bool streaming_ = false;
  Size size_;
  PixelFormat format_ = PixelFormat::Unknown;
  std::vector<Buffer> buffers_;
  CaptureStats stats_;
  std::optional<uint32_t> held_buffer_;
  uint64_t sequence_ = 0;
};

} // namespace cv
