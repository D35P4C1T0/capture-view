#include "cli.hpp"
#include "frame.hpp"
#include "v4l2_output.hpp"

#include <cstddef>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    throw cv::AppError(message);
  }
}

cv::CliOptions parse(std::vector<std::string> args) {
  std::vector<char*> argv;
  argv.reserve(args.size());
  for (auto& arg : args) {
    argv.push_back(arg.data());
  }
  return cv::parse_cli(static_cast<int>(argv.size()), argv.data());
}

void test_cli_parses_capture_options() {
  cv::CliOptions options = parse({"capture-view",
                                  "--video",
                                  "/dev/video2",
                                  "--size",
                                  "1280x720",
                                  "--fps",
                                  "60",
                                  "--format",
                                  "nv12",
                                  "--no-vsync"});
  require(options.video_device == "/dev/video2", "video device was not parsed");
  require(options.size.width == 1280 && options.size.height == 720, "size was not parsed");
  require(options.fps == 60, "fps was not parsed");
  require(options.format == cv::PixelFormat::Nv12, "format was not parsed");
  require(!options.vsync, "no-vsync was not parsed");
}

void test_cli_rejects_invalid_size() {
  bool threw = false;
  try {
    (void)parse({"capture-view", "--size", "1920"});
  } catch (const cv::AppError&) {
    threw = true;
  }
  require(threw, "invalid size did not throw");
}

void test_rgba_to_output_formats_resize_buffers() {
  cv::RgbaFrame frame;
  frame.size = {2, 2};
  frame.pixels = {
      std::byte{255}, std::byte{0},   std::byte{0},   std::byte{255},
      std::byte{0},   std::byte{255}, std::byte{0},   std::byte{255},
      std::byte{0},   std::byte{0},   std::byte{255}, std::byte{255},
      std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255},
  };

  std::vector<std::byte> yuyv;
  cv::convert_rgba_to_yuyv(frame, yuyv);
  require(yuyv.size() == 8, "YUYV buffer size is wrong");

  std::vector<std::byte> nv12;
  cv::convert_rgba_to_nv12(frame, nv12);
  require(nv12.size() == 6, "NV12 buffer size is wrong");
}

void test_output_stats_default_to_zero() {
  const cv::V4l2OutputStats stats;
  require(stats.frames == 0, "output frame stats default is wrong");
  require(stats.dropped_writes == 0, "output drop stats default is wrong");
}

} // namespace

int main() {
  try {
    test_cli_parses_capture_options();
    test_cli_rejects_invalid_size();
    test_rgba_to_output_formats_resize_buffers();
    test_output_stats_default_to_zero();
  } catch (const std::exception& error) {
    std::cerr << "test failed: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
