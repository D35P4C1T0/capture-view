#pragma once

#include "common.hpp"

#include <optional>
#include <string>

namespace cv {

struct CliOptions {
  bool list_devices = false;
  bool list_audio = false;
  bool doctor = false;
  bool diagnostic_bundle = false;
  bool wizard = false;
  bool save_config = false;
  bool list_profiles = false;
  bool init_profiles = false;
  bool verbose = false;
  bool fullscreen = false;
  bool vsync = false;
  bool test_pattern = false;
  bool no_config = false;
  bool audio_monitor = false;
  bool audio_test_tone = false;
  bool audio_autotune = true;
  bool muted = false;
  uint32_t buffer_count = 3;
  std::string video_device = "/dev/video0";
  std::string audio_input;
  std::string audio_output;
  std::string audio_virtual_source;
  std::string latency_mode;
  std::string log_file;
  std::string profile;
  std::string config_file;
  std::string output_scaling = "fit";
  std::string diagnostic_bundle_file;
  Size size{1280, 720};
  uint32_t fps = 60;
  PixelFormat format = PixelFormat::Auto;
  uint32_t audio_buffer_ms = 20;
  uint32_t audio_quantum = 0;
  int32_t audio_delay_ms = 0;
  float volume = 1.0F;
  std::optional<uint32_t> benchmark_seconds;
};

CliOptions parse_cli(int argc, char** argv);
CliOptions parse_cli(int argc, char** argv, CliOptions options);
void print_usage(const char* argv0);

} // namespace cv
