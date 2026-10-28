#include "cli.hpp"
#include "frame.hpp"
#include "config.hpp"

#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <random>
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
  cv::CliOptions options = parse({"capture-view", "--video", "/dev/video2", "--size", "1280x720", "--fps", "60",
                                  "--format", "nv12", "--no-vsync"});
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

void test_h264_format_mapping() {
  const cv::CliOptions options = parse({"capture-view", "--format", "h264"});
  require(options.format == cv::PixelFormat::H264, "H.264 format was not parsed");
  require(cv::pixel_format_from_v4l2(cv::pixel_format_to_v4l2(cv::PixelFormat::H264)) == cv::PixelFormat::H264,
          "H.264 V4L2 mapping did not round-trip");
}

void test_ultra_latency_preset() {
  const cv::CliOptions options = parse({"capture-view", "--latency-mode", "ultra"});
  require(!options.vsync, "ultra mode enabled vsync");
  require(options.buffer_count == 2, "ultra mode did not select two video buffers");
  require(options.audio_buffer_ms == 3, "ultra mode audio buffer is not 3ms");
  require(options.frame_pacing == "immediate", "ultra mode pacing is not immediate");
}

void test_rgba_to_output_formats_resize_buffers() {
  cv::RgbaFrame frame;
  frame.size = {2, 2};
  frame.pixels = {
      std::byte{255}, std::byte{0},   std::byte{0},   std::byte{255}, std::byte{0},   std::byte{255},
      std::byte{0},   std::byte{255}, std::byte{0},   std::byte{0},   std::byte{255}, std::byte{255},
      std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255},
  };

  std::vector<std::byte> yuyv;
  cv::convert_rgba_to_yuyv(frame, yuyv);
  require(yuyv.size() == 8, "YUYV buffer size is wrong");

  std::vector<std::byte> nv12;
  cv::convert_rgba_to_nv12(frame, nv12);
  require(nv12.size() == 6, "NV12 buffer size is wrong");
}

void test_padded_raw_frames() {
  const std::vector<std::byte> yuyv{
      std::byte{16}, std::byte{128}, std::byte{16}, std::byte{128},
      std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
      std::byte{235}, std::byte{128}, std::byte{235}, std::byte{128}};
  cv::RgbaFrame rgba;
  cv::FrameView frame{yuyv, cv::PixelFormat::Yuyv, {2, 2}, 7, {}, 8};
  cv::convert_yuyv_to_rgba(frame, rgba);
  require(rgba.sequence == 7 && rgba.pixels[0] == std::byte{0} && rgba.pixels[8] == std::byte{255} &&
              rgba.pixels[9] == std::byte{255} && rgba.pixels[10] == std::byte{255},
          "padded YUYV rows were not decoded correctly");

  const std::vector<std::byte> nv12{
      std::byte{16}, std::byte{16}, std::byte{0}, std::byte{0},
      std::byte{235}, std::byte{235}, std::byte{0}, std::byte{0}, std::byte{128}, std::byte{128}};
  frame = {nv12, cv::PixelFormat::Nv12, {2, 2}, 8, {}, 4};
  cv::convert_nv12_to_rgba(frame, rgba);
  require(rgba.sequence == 8 && rgba.pixels[0] == std::byte{0} && rgba.pixels[8] == std::byte{255} &&
              rgba.pixels[9] == std::byte{255} && rgba.pixels[10] == std::byte{255},
          "padded NV12 rows or chroma offset were not decoded correctly");
  for (const auto& invalid : std::vector<cv::FrameView>{
           {nv12, cv::PixelFormat::Nv12, {2, 2}, 0, {}, 8},
           {yuyv, cv::PixelFormat::Yuyv, {2, 2}, 0, {}, 2},
           {yuyv, cv::PixelFormat::Yuyv, {1, 1}},
           {nv12, cv::PixelFormat::Nv12, {2, 1}}}) {
    bool threw = false;
    try { (void)cv::raw_frame_stride(invalid); } catch (const cv::AppError&) { threw = true; }
    require(threw, "invalid raw layout was accepted");
  }
}

void test_config_and_viewer_handoff() {
  const auto previous_dir = std::filesystem::current_path();
  const auto directory = std::filesystem::temp_directory_path() /
                         ("capture-view-tests-" + std::to_string(std::random_device{}()));
  require(std::filesystem::create_directory(directory), "could not create config test directory");
  try {
    std::filesystem::current_path(directory);
    cv::CliOptions options;
    options.config_file = "settings.toml";
    options.audio_virtual_source = "HDMI #1 \"quoted\" \\name\nnext\tline\r";
    options.fullscreen = true;
    options.borderless = true;
    options.audio_monitor = true;
    cv::set_config_location(options);
    cv::save_config(options);
    cv::CliOptions loaded;
    cv::load_config(loaded);
    require(loaded.audio_virtual_source == options.audio_virtual_source && loaded.fullscreen,
            "relative config save or quoted strings did not round-trip");
    options.fullscreen = false;
    cv::save_config(options);
    loaded = {};
    cv::load_config(loaded);
    require(!loaded.fullscreen, "existing config was not replaced");
    {
      std::ofstream file("settings.toml", std::ios::app);
      file << "audio_input = \"input #2 \\\"name\\\"\" # real comment\n";
    }
    loaded = {};
    cv::load_config(loaded);
    require(loaded.audio_input == "input #2 \"name\"", "quoted hash or trailing comment was parsed incorrectly");

    options.profile = "console";
    options.no_config = false;
    options.fullscreen = false;
    options.borderless = false;
    options.audio_monitor = false;
    options.audio_input = "card input";
    options.audio_output = "headphones";
    options.video_output = "/dev/video10";
    options.video_output_format = "nv12";
    options.latency_mode = "ultra";
    options.buffer_count = 3;
    options.audio_buffer_ms = 15;
    options.frame_pacing = "sleep";
    options.audio_quantum = 1024;
    options.audio_delay_ms = 25;
    options.audio_autotune = false;
    options.volume = 0.75F;
    options.muted = true;
    options.log_file = "viewer.log";
    options.save_config = true;
    options.verbose = true;
    auto args = cv::viewer_arguments(options);
    args.insert(args.begin(), "capture-view");
    const auto child = parse(args);
    require(child.no_load_config && !child.no_config && child.config_file == options.config_file &&
                child.profile == options.profile, "viewer changed config loading or save destination");
    require(!child.fullscreen && !child.borderless && !child.audio_monitor &&
                child.audio_virtual_source == options.audio_virtual_source,
            "unchecked controls or remembered virtual source were lost");
    require(child.audio_input == options.audio_input && child.audio_output == options.audio_output &&
                child.video_output == options.video_output && child.video_output_format == options.video_output_format,
            "viewer lost selected input/output routes");
    require(child.latency_mode == options.latency_mode && child.buffer_count == 3 && child.audio_buffer_ms == 15 &&
                child.frame_pacing == "sleep" && child.audio_quantum == 1024 && child.audio_delay_ms == 25,
            "preset overwrote explicit viewer settings");
    require(!child.audio_autotune && child.volume == 0.75F && child.muted && child.log_file == options.log_file &&
                child.save_config && child.verbose, "viewer lost audio or logging settings");
    loaded = child;
    cv::load_config(loaded);
    require(!loaded.borderless && !loaded.audio_monitor, "viewer reloaded conflicting saved settings");
    options.volume = 0.0000001F;
    args = cv::viewer_arguments(options);
    args.insert(args.begin(), "capture-view");
    require(parse(args).volume == options.volume, "viewer rounded a small volume to zero");
    options.no_config = true;
    args = cv::viewer_arguments(options);
    args.insert(args.begin(), "capture-view");
    require(parse(args).no_config, "viewer lost --no-config");
    options.no_config = false;
    options.config_file = "blocked.toml";
    std::filesystem::create_directory("blocked.toml");
    { std::ofstream marker("blocked.toml/keep"); marker << "keep"; }
    cv::set_config_location(options);
    bool threw = false;
    try { cv::save_config(options); } catch (const std::exception&) { threw = true; }
    require(threw && std::filesystem::exists("blocked.toml/keep"), "failed replacement damaged the destination");
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
      require(entry.path().extension() != ".tmp", "config save left a temporary file");
    }
  } catch (...) {
    std::filesystem::current_path(previous_dir);
    std::filesystem::remove_all(directory);
    throw;
  }
  cv::set_config_location({});
  std::filesystem::current_path(previous_dir);
  std::filesystem::remove_all(directory);
}

} // namespace

int main() {
  try {
    test_cli_parses_capture_options();
    test_cli_rejects_invalid_size();
    test_h264_format_mapping();
    test_ultra_latency_preset();
    test_rgba_to_output_formats_resize_buffers();
    test_padded_raw_frames();
    test_config_and_viewer_handoff();
  } catch (const std::exception& error) {
    std::cerr << "test failed: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
