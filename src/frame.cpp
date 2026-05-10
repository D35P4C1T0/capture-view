#include "frame.hpp"

#include <algorithm>
#include <cstring>
#include <linux/videodev2.h>

namespace cv {

namespace {

uint8_t clamp_u8(int value) {
  return static_cast<uint8_t>(std::clamp(value, 0, 255));
}

} // namespace

std::string to_string(PixelFormat format) {
  switch (format) {
  case PixelFormat::Auto:
    return "auto";
  case PixelFormat::Mjpeg:
    return "mjpeg";
  case PixelFormat::Yuyv:
    return "yuyv";
  case PixelFormat::Nv12:
    return "nv12";
  case PixelFormat::Unknown:
    return "unknown";
  }
  return "unknown";
}

PixelFormat pixel_format_from_cli(const std::string& value) {
  if (value == "auto" || value == "AUTO") {
    return PixelFormat::Auto;
  }
  if (value == "mjpeg" || value == "MJPEG") {
    return PixelFormat::Mjpeg;
  }
  if (value == "yuyv" || value == "YUYV") {
    return PixelFormat::Yuyv;
  }
  if (value == "nv12" || value == "NV12") {
    return PixelFormat::Nv12;
  }
  return PixelFormat::Unknown;
}

uint32_t pixel_format_to_v4l2(PixelFormat format) {
  switch (format) {
  case PixelFormat::Auto:
    return 0;
  case PixelFormat::Mjpeg:
    return V4L2_PIX_FMT_MJPEG;
  case PixelFormat::Yuyv:
    return V4L2_PIX_FMT_YUYV;
  case PixelFormat::Nv12:
    return V4L2_PIX_FMT_NV12;
  case PixelFormat::Unknown:
    return 0;
  }
  return 0;
}

PixelFormat pixel_format_from_v4l2(uint32_t fourcc) {
  switch (fourcc) {
  case V4L2_PIX_FMT_MJPEG:
    return PixelFormat::Mjpeg;
  case V4L2_PIX_FMT_YUYV:
    return PixelFormat::Yuyv;
  case V4L2_PIX_FMT_NV12:
    return PixelFormat::Nv12;
  default:
    return PixelFormat::Unknown;
  }
}

std::string fourcc_to_string(uint32_t fourcc) {
  std::string result(4, ' ');
  std::memcpy(result.data(), &fourcc, 4);
  return result;
}

void convert_yuyv_to_rgba(FrameView src, RgbaFrame& dst) {
  const size_t expected = static_cast<size_t>(src.size.width) * src.size.height * 2;
  if (src.bytes.size() < expected) {
    throw AppError("short YUYV frame");
  }

  dst.size = src.size;
  dst.sequence = src.sequence;
  dst.pixels.resize(static_cast<size_t>(src.size.width) * src.size.height * 4);

  const auto* in = reinterpret_cast<const uint8_t*>(src.bytes.data());
  auto* out = reinterpret_cast<uint8_t*>(dst.pixels.data());

  for (size_t i = 0, o = 0; i < expected; i += 4, o += 8) {
    const int y0 = static_cast<int>(in[i + 0]);
    const int u = static_cast<int>(in[i + 1]) - 128;
    const int y1 = static_cast<int>(in[i + 2]);
    const int v = static_cast<int>(in[i + 3]) - 128;

    const auto write_pixel = [&](size_t offset, int y) {
      const int c = y - 16;
      out[offset + 0] = clamp_u8((298 * c + 409 * v + 128) >> 8);
      out[offset + 1] = clamp_u8((298 * c - 100 * u - 208 * v + 128) >> 8);
      out[offset + 2] = clamp_u8((298 * c + 516 * u + 128) >> 8);
      out[offset + 3] = 255;
    };

    write_pixel(o + 0, y0);
    write_pixel(o + 4, y1);
  }
}

void convert_nv12_to_rgba(FrameView src, RgbaFrame& dst) {
  const size_t y_size = static_cast<size_t>(src.size.width) * src.size.height;
  const size_t expected = y_size + y_size / 2;
  if (src.bytes.size() < expected) {
    throw AppError("short NV12 frame");
  }

  dst.size = src.size;
  dst.sequence = src.sequence;
  dst.pixels.resize(static_cast<size_t>(src.size.width) * src.size.height * 4);

  const auto* y_plane = reinterpret_cast<const uint8_t*>(src.bytes.data());
  const auto* uv_plane = y_plane + y_size;
  auto* out = reinterpret_cast<uint8_t*>(dst.pixels.data());

  for (uint32_t y = 0; y < src.size.height; ++y) {
    for (uint32_t x = 0; x < src.size.width; ++x) {
      const size_t y_index = static_cast<size_t>(y) * src.size.width + x;
      const size_t uv_index = static_cast<size_t>(y / 2) * src.size.width + (x & ~1U);
      const int yy = static_cast<int>(y_plane[y_index]);
      const int u = static_cast<int>(uv_plane[uv_index]) - 128;
      const int v = static_cast<int>(uv_plane[uv_index + 1]) - 128;
      const int c = yy - 16;
      const size_t o = y_index * 4;
      out[o + 0] = clamp_u8((298 * c + 409 * v + 128) >> 8);
      out[o + 1] = clamp_u8((298 * c - 100 * u - 208 * v + 128) >> 8);
      out[o + 2] = clamp_u8((298 * c + 516 * u + 128) >> 8);
      out[o + 3] = 255;
    }
  }
}

} // namespace cv
