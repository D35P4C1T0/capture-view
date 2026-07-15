#include "cli.hpp"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace cv {

namespace {

void apply_latency_mode(CliOptions& options, const std::string& mode) {
  options.latency_mode = mode;
  if (mode == "ultra") {
    options.vsync = false;
    options.buffer_count = 2;
    options.audio_buffer_ms = 3;
    options.frame_pacing = "immediate";
  } else if (mode == "low") {
    options.vsync = false;
    options.buffer_count = 3;
    options.audio_buffer_ms = 10;
    options.frame_pacing = "yield";
  } else if (mode == "balanced") {
    options.vsync = true;
    options.buffer_count = 3;
    options.audio_buffer_ms = 20;
    options.frame_pacing = "sleep";
  } else {
    throw AppError("latency mode must be ultra, low, or balanced");
  }
}

uint32_t parse_u32(std::string_view value, const char* name) {
  uint32_t out = 0;
  const auto* begin = value.data();
  const auto* end = begin + value.size();
  const auto [ptr, ec] = std::from_chars(begin, end, out);
  if (ec != std::errc{} || ptr != end) {
    throw AppError(std::string("invalid ") + name + ": " + std::string(value));
  }
  return out;
}

int32_t parse_i32(std::string_view value, const char* name) {
  int32_t out = 0;
  const auto* begin = value.data();
  const auto* end = begin + value.size();
  const auto [ptr, ec] = std::from_chars(begin, end, out);
  if (ec != std::errc{} || ptr != end) {
    throw AppError(std::string("invalid ") + name + ": " + std::string(value));
  }
  return out;
}

Size parse_size(std::string_view value) {
  const size_t x = value.find('x');
  if (x == std::string_view::npos) {
    throw AppError("size must be WIDTHxHEIGHT");
  }
  return {parse_u32(value.substr(0, x), "width"), parse_u32(value.substr(x + 1), "height")};
}

std::string require_value(int& index, int argc, char** argv, const char* option) {
  if (index + 1 >= argc) {
    throw AppError(std::string("missing value for ") + option);
  }
  ++index;
  return argv[index];
}

} // namespace

void print_usage(const char* argv0) {
  std::cout << "Usage: " << argv0 << " [options]\n"
            << "\n"
            << "Options:\n"
            << "  --list-devices\n"
            << "  --list-audio\n"
            << "  --doctor\n"
            << "  --diagnostic-bundle PATH\n"
            << "  --gtk\n"
            << "  --wizard\n"
            << "  --save-config\n"
            << "  --list-profiles\n"
            << "  --init-profiles\n"
            << "  --video /dev/video0\n"
            << "  --video-output /dev/video10\n"
            << "  --video-output-format yuyv|nv12|rgba|mjpeg\n"
            << "  --audio-input \"name or id\"\n"
            << "  --audio-output \"name or id\"\n"
            << "  --audio-virtual-source NAME\n"
            << "  --audio-monitor\n"
            << "  --audio-test-tone\n"
            << "  --mute\n"
            << "  --size 1920x1080\n"
            << "  --fps 60\n"
            << "  --format auto|mjpeg|yuyv|nv12\n"
            << "  --latency-mode ultra|low|balanced\n"
            << "  --frame-pacing immediate|yield|sleep|adaptive\n"
            << "  --render-backend sdl|opengl\n"
            << "  --fullscreen\n"
            << "  --borderless\n"
            << "  --no-vsync\n"
            << "  --vsync\n"
            << "  --buffers 2|3\n"
            << "  --test-pattern\n"
            << "  --audio-buffer-ms 5\n"
            << "  --audio-quantum FRAMES\n"
            << "  --audio-delay-ms -200..200\n"
            << "  --no-audio-autotune\n"
            << "  --volume 1.0\n"
            << "  --no-config\n"
            << "  --profile NAME\n"
            << "  --config PATH\n"
            << "  --verbose\n"
            << "  --log-file PATH\n"
            << "  --version\n"
            << "  --help\n";
}

CliOptions parse_cli(int argc, char** argv) { return parse_cli(argc, argv, CliOptions{}); }

CliOptions parse_cli(int argc, char** argv, CliOptions options) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--list-devices") {
      options.list_devices = true;
    } else if (arg == "--list-audio") {
      options.list_audio = true;
    } else if (arg == "--doctor") {
      options.doctor = true;
    } else if (arg == "--diagnostic-bundle") {
      options.diagnostic_bundle = true;
      options.diagnostic_bundle_file = require_value(i, argc, argv, "--diagnostic-bundle");
    } else if (arg == "--gtk") {
      options.gtk_ui = true;
    } else if (arg == "--wizard") {
      options.wizard = true;
    } else if (arg == "--save-config") {
      options.save_config = true;
    } else if (arg == "--list-profiles") {
      options.list_profiles = true;
    } else if (arg == "--init-profiles") {
      options.init_profiles = true;
    } else if (arg == "--verbose") {
      options.verbose = true;
    } else if (arg == "--log-file") {
      options.log_file = require_value(i, argc, argv, "--log-file");
    } else if (arg == "--fullscreen") {
      options.fullscreen = true;
    } else if (arg == "--borderless") {
      options.borderless = true;
    } else if (arg == "--no-vsync") {
      options.vsync = false;
    } else if (arg == "--vsync") {
      options.vsync = true;
    } else if (arg == "--test-pattern") {
      options.test_pattern = true;
    } else if (arg == "--no-config") {
      options.no_config = true;
    } else if (arg == "--profile") {
      options.profile = require_value(i, argc, argv, "--profile");
    } else if (arg == "--config") {
      options.config_file = require_value(i, argc, argv, "--config");
    } else if (arg == "--audio-monitor") {
      options.audio_monitor = true;
    } else if (arg == "--audio-test-tone") {
      options.audio_monitor = true;
      options.audio_test_tone = true;
    } else if (arg == "--mute") {
      options.muted = true;
    } else if (arg == "--video") {
      options.video_device = require_value(i, argc, argv, "--video");
    } else if (arg == "--video-output") {
      options.video_output = require_value(i, argc, argv, "--video-output");
    } else if (arg == "--video-output-format") {
      options.video_output_format = require_value(i, argc, argv, "--video-output-format");
      if (options.video_output_format != "yuyv" && options.video_output_format != "nv12" &&
          options.video_output_format != "rgba" && options.video_output_format != "mjpeg") {
        throw AppError("--video-output-format must be yuyv, nv12, rgba, or mjpeg");
      }
    } else if (arg == "--audio-input") {
      options.audio_input = require_value(i, argc, argv, "--audio-input");
    } else if (arg == "--audio-output") {
      options.audio_output = require_value(i, argc, argv, "--audio-output");
    } else if (arg == "--audio-virtual-source") {
      options.audio_virtual_source = require_value(i, argc, argv, "--audio-virtual-source");
      options.audio_monitor = true;
    } else if (arg == "--size") {
      options.size = parse_size(require_value(i, argc, argv, "--size"));
    } else if (arg == "--fps") {
      options.fps = parse_u32(require_value(i, argc, argv, "--fps"), "fps");
    } else if (arg == "--format") {
      options.format = pixel_format_from_cli(require_value(i, argc, argv, "--format"));
      if (options.format == PixelFormat::Unknown) {
        throw AppError("format must be auto, mjpeg, yuyv, or nv12");
      }
    } else if (arg == "--latency-mode") {
      apply_latency_mode(options, require_value(i, argc, argv, "--latency-mode"));
    } else if (arg == "--frame-pacing") {
      options.frame_pacing = require_value(i, argc, argv, "--frame-pacing");
      if (options.frame_pacing != "immediate" && options.frame_pacing != "yield" && options.frame_pacing != "sleep" &&
          options.frame_pacing != "adaptive") {
        throw AppError("--frame-pacing must be immediate, yield, sleep, or adaptive");
      }
    } else if (arg == "--render-backend") {
      options.render_backend = require_value(i, argc, argv, "--render-backend");
      if (options.render_backend != "sdl" && options.render_backend != "opengl") {
        throw AppError("--render-backend must be sdl or opengl");
      }
    } else if (arg == "--buffers") {
      options.buffer_count = parse_u32(require_value(i, argc, argv, "--buffers"), "buffers");
      if (options.buffer_count < 2 || options.buffer_count > 3) {
        throw AppError("--buffers must be 2 or 3 for low-latency mode");
      }
    } else if (arg == "--audio-buffer-ms") {
      options.audio_buffer_ms = parse_u32(require_value(i, argc, argv, "--audio-buffer-ms"), "audio buffer ms");
    } else if (arg == "--audio-quantum") {
      options.audio_quantum = parse_u32(require_value(i, argc, argv, "--audio-quantum"), "audio quantum");
    } else if (arg == "--audio-delay-ms") {
      options.audio_delay_ms =
          std::clamp(parse_i32(require_value(i, argc, argv, "--audio-delay-ms"), "audio delay ms"), -200, 200);
    } else if (arg == "--no-audio-autotune") {
      options.audio_autotune = false;
    } else if (arg == "--volume") {
      options.volume = std::stof(require_value(i, argc, argv, "--volume"));
    } else if (arg == "--help" || arg == "-h") {
      print_usage(argv[0]);
      std::exit(0);
    } else if (arg == "--version") {
      std::cout << "capture-view 0.1.0\n";
      std::exit(0);
    } else {
      throw AppError("unknown option: " + arg);
    }
  }
  return options;
}

} // namespace cv
