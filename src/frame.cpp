#include "frame.hpp"

#include <algorithm>
#include <cstring>
namespace cv {

namespace {

constexpr uint32_t fourcc(char a, char b, char c, char d) {
  return static_cast<uint32_t>(static_cast<unsigned char>(a)) |
         (static_cast<uint32_t>(static_cast<unsigned char>(b)) << 8U) |
         (static_cast<uint32_t>(static_cast<unsigned char>(c)) << 16U) |
         (static_cast<uint32_t>(static_cast<unsigned char>(d)) << 24U);
}

constexpr uint32_t kPixFmtMjpeg = fourcc('M', 'J', 'P', 'G');
constexpr uint32_t kPixFmtH264 = fourcc('H', '2', '6', '4');
constexpr uint32_t kPixFmtYuyv = fourcc('Y', 'U', 'Y', 'V');
constexpr uint32_t kPixFmtNv12 = fourcc('N', 'V', '1', '2');

uint8_t clamp_u8(int value) {
  return static_cast<uint8_t>(std::clamp(value, 0, 255));
}

} // namespace

std::string to_string(PixelFormat format) {
  switch (format) {
  case PixelFormat::Auto:
    return "auto";
  case PixelFormat::H264:
    return "h264";
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
  if (value == "h264" || value == "H264") {
    return PixelFormat::H264;
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
  case PixelFormat::H264:
    return kPixFmtH264;
  case PixelFormat::Mjpeg:
    return kPixFmtMjpeg;
  case PixelFormat::Yuyv:
    return kPixFmtYuyv;
  case PixelFormat::Nv12:
    return kPixFmtNv12;
  case PixelFormat::Unknown:
    return 0;
  }
  return 0;
}

PixelFormat pixel_format_from_v4l2(uint32_t fourcc) {
  switch (fourcc) {
  case kPixFmtH264:
    return PixelFormat::H264;
  case kPixFmtMjpeg:
    return PixelFormat::Mjpeg;
  case kPixFmtYuyv:
    return PixelFormat::Yuyv;
  case kPixFmtNv12:
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

void convert_rgba_to_yuyv(const RgbaFrame& src, std::vector<std::byte>& dst) {
  if ((src.size.width % 2) != 0) {
    throw AppError("YUYV output requires even frame width");
  }
  const size_t expected_rgba = static_cast<size_t>(src.size.width) * src.size.height * 4;
  if (src.pixels.size() < expected_rgba) {
    throw AppError("short RGBA frame");
  }

  dst.resize(static_cast<size_t>(src.size.width) * src.size.height * 2);
  const auto* in = reinterpret_cast<const uint8_t*>(src.pixels.data());
  auto* out = reinterpret_cast<uint8_t*>(dst.data());

  for (size_t i = 0, o = 0; i < expected_rgba; i += 8, o += 4) {
    const int r0 = static_cast<int>(in[i + 0]);
    const int g0 = static_cast<int>(in[i + 1]);
    const int b0 = static_cast<int>(in[i + 2]);
    const int r1 = static_cast<int>(in[i + 4]);
    const int g1 = static_cast<int>(in[i + 5]);
    const int b1 = static_cast<int>(in[i + 6]);

    const int y0 = ((66 * r0 + 129 * g0 + 25 * b0 + 128) >> 8) + 16;
    const int y1 = ((66 * r1 + 129 * g1 + 25 * b1 + 128) >> 8) + 16;
    const int u0 = ((-38 * r0 - 74 * g0 + 112 * b0 + 128) >> 8) + 128;
    const int u1 = ((-38 * r1 - 74 * g1 + 112 * b1 + 128) >> 8) + 128;
    const int v0 = ((112 * r0 - 94 * g0 - 18 * b0 + 128) >> 8) + 128;
    const int v1 = ((112 * r1 - 94 * g1 - 18 * b1 + 128) >> 8) + 128;

    out[o + 0] = clamp_u8(y0);
    out[o + 1] = clamp_u8((u0 + u1) / 2);
    out[o + 2] = clamp_u8(y1);
    out[o + 3] = clamp_u8((v0 + v1) / 2);
  }
}

void convert_rgba_to_nv12(const RgbaFrame& src, std::vector<std::byte>& dst) {
  if ((src.size.width % 2) != 0 || (src.size.height % 2) != 0) {
    throw AppError("NV12 output requires even frame size");
  }
  const size_t expected_rgba = static_cast<size_t>(src.size.width) * src.size.height * 4;
  if (src.pixels.size() < expected_rgba) {
    throw AppError("short RGBA frame");
  }

  const size_t y_size = static_cast<size_t>(src.size.width) * src.size.height;
  dst.resize(y_size + y_size / 2);
  const auto* in = reinterpret_cast<const uint8_t*>(src.pixels.data());
  auto* y_plane = reinterpret_cast<uint8_t*>(dst.data());
  auto* uv_plane = y_plane + y_size;

  const auto yuv_for = [&](uint32_t x, uint32_t y, int& yy, int& u, int& v) {
    const size_t i = (static_cast<size_t>(y) * src.size.width + x) * 4;
    const int r = static_cast<int>(in[i + 0]);
    const int g = static_cast<int>(in[i + 1]);
    const int b = static_cast<int>(in[i + 2]);
    yy = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
    u = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
    v = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
  };

  for (uint32_t y = 0; y < src.size.height; ++y) {
    for (uint32_t x = 0; x < src.size.width; ++x) {
      int yy = 0;
      int u = 0;
      int v = 0;
      yuv_for(x, y, yy, u, v);
      y_plane[static_cast<size_t>(y) * src.size.width + x] = clamp_u8(yy);
    }
  }

  for (uint32_t y = 0; y < src.size.height; y += 2) {
    for (uint32_t x = 0; x < src.size.width; x += 2) {
      int yy = 0;
      int u0 = 0;
      int v0 = 0;
      int u1 = 0;
      int v1 = 0;
      int u2 = 0;
      int v2 = 0;
      int u3 = 0;
      int v3 = 0;
      yuv_for(x, y, yy, u0, v0);
      yuv_for(x + 1, y, yy, u1, v1);
      yuv_for(x, y + 1, yy, u2, v2);
      yuv_for(x + 1, y + 1, yy, u3, v3);
      const size_t uv = static_cast<size_t>(y / 2) * src.size.width + x;
      uv_plane[uv + 0] = clamp_u8((u0 + u1 + u2 + u3) / 4);
      uv_plane[uv + 1] = clamp_u8((v0 + v1 + v2 + v3) / 4);
    }
  }
}

} // namespace cv
