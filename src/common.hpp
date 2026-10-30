#pragma once

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace cv {

using Clock = std::chrono::steady_clock;

struct AppError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct Size {
  uint32_t width = 1280;
  uint32_t height = 720;
};

enum class PixelFormat {
  Auto,
  H264,
  Mjpeg,
  Yuyv,
  Nv12,
  Unknown,
};

[[nodiscard]] std::string to_string(PixelFormat format);
[[nodiscard]] PixelFormat pixel_format_from_cli(const std::string& value);
[[nodiscard]] uint32_t pixel_format_to_v4l2(PixelFormat format);
[[nodiscard]] PixelFormat pixel_format_from_v4l2(uint32_t fourcc);
[[nodiscard]] std::string fourcc_to_string(uint32_t fourcc);

} // namespace cv
