#include "wizard.hpp"

#include "audio_devices.hpp"
#include "common.hpp"
#include "log.hpp"
#include "v4l2_discovery.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

namespace cv {

namespace {

struct ModeCandidate {
  Size size;
  uint32_t fps = 0;
  PixelFormat format = PixelFormat::Unknown;
  int score = 0;
  std::string reason;
};

bool terminal_input() {
  return ::isatty(STDIN_FILENO);
}

std::string prompt_line(const std::string& prompt) {
  std::cout << prompt << std::flush;
  std::string line;
  std::getline(std::cin, line);
  return line;
}

size_t prompt_index(size_t count, size_t default_index) {
  const std::string line = prompt_line("Select [1-" + std::to_string(count) +
                                       ", Enter=" + std::to_string(default_index + 1) + "]: ");
  if (line.empty()) {
    return default_index;
  }
  size_t selected = 0;
  try {
    selected = static_cast<size_t>(std::stoul(line));
  } catch (...) {
    throw AppError("invalid wizard selection: " + line);
  }
  if (selected == 0 || selected > count) {
    throw AppError("wizard selection out of range: " + line);
  }
  return selected - 1;
}

bool prompt_yes_no(const std::string& prompt, bool default_value) {
  const std::string suffix = default_value ? " [Y/n]: " : " [y/N]: ";
  const std::string line = prompt_line(prompt + suffix);
  if (line.empty()) {
    return default_value;
  }
  return line == "y" || line == "Y" || line == "yes" || line == "YES";
}

std::vector<AudioDeviceInfo> filter_audio(const std::vector<AudioDeviceInfo>& devices,
                                          const std::string& media_class) {
  std::vector<AudioDeviceInfo> out;
  for (const auto& device : devices) {
    if (device.media_class == media_class) {
      out.push_back(device);
    }
  }
  return out;
}

std::string audio_label(const AudioDeviceInfo& device) {
  std::string label = std::to_string(device.id) + "  " + audio_device_display_name(device);
  label += "  [" + audio_device_detail(device) + "]";
  return label;
}

std::string choose_audio(const std::vector<AudioDeviceInfo>& devices, const std::string& title) {
  if (devices.empty()) {
    throw AppError("no PipeWire " + title + " nodes found");
  }
  std::cout << "\n" << title << "\n";
  for (size_t i = 0; i < devices.size(); ++i) {
    std::cout << "  " << (i + 1) << ". " << audio_label(devices[i]);
    if (i == 0) {
      std::cout << "  [suggested]";
    }
    std::cout << "\n";
  }
  return devices[prompt_index(devices.size(), 0)].name;
}

std::vector<ModeCandidate> collect_modes(const VideoDeviceInfo& device) {
  std::vector<ModeCandidate> modes;
  for (const auto& format : device.formats) {
    const PixelFormat pixel_format = pixel_format_from_v4l2(format.fourcc);
    if (pixel_format == PixelFormat::Unknown) {
      continue;
    }
    for (const auto& size : format.sizes) {
      if (size.intervals.empty()) {
        modes.push_back({size.size, 60, pixel_format, 0, {}});
        continue;
      }
      for (const auto& interval : size.intervals) {
        if (interval.numerator == 0) {
          continue;
        }
        modes.push_back({size.size, interval.denominator / interval.numerator, pixel_format, 0, {}});
      }
    }
  }

  for (auto& mode : modes) {
    const bool mode_1080p = mode.size.width == 1920 && mode.size.height == 1080;
    const bool mode_720p = mode.size.width == 1280 && mode.size.height == 720;
    const bool mode_60 = mode.fps >= 60;
    const int format_score = mode.format == PixelFormat::Yuyv ? 30 :
                             mode.format == PixelFormat::Nv12 ? 25 :
                             mode.format == PixelFormat::Mjpeg ? 20 : 0;
    mode.score = (mode_1080p ? 10000 : 0) + (mode_720p ? 4000 : 0) +
                 (mode_60 ? 3000 : 0) +
                 static_cast<int>(mode.size.width * mode.size.height / 1000) +
                 static_cast<int>(mode.fps) * 10 + format_score;
    if (mode.format == PixelFormat::Mjpeg) {
      mode.reason = "best practical USB bandwidth for 1080p60";
    } else {
      mode.reason = "raw format: lowest decode latency if USB bandwidth holds";
    }
  }

  std::ranges::sort(modes, [](const ModeCandidate& a, const ModeCandidate& b) {
    return a.score > b.score;
  });
  modes.erase(std::unique(modes.begin(), modes.end(), [](const ModeCandidate& a, const ModeCandidate& b) {
                return a.size.width == b.size.width && a.size.height == b.size.height &&
                       a.fps == b.fps && a.format == b.format;
              }),
              modes.end());
  return modes;
}

void apply_latency(CliOptions& options, const std::string& mode) {
  options.latency_mode = mode;
  if (mode == "ultra") {
    options.vsync = false;
    options.buffer_count = 2;
    options.audio_buffer_ms = 20;
  } else if (mode == "low") {
    options.vsync = false;
    options.buffer_count = 3;
    options.audio_buffer_ms = 30;
  } else {
    options.vsync = true;
    options.buffer_count = 3;
    options.audio_buffer_ms = 40;
  }
}

} // namespace

void run_wizard(CliOptions& options) {
  if (!terminal_input()) {
    throw AppError("--wizard needs interactive terminal input");
  }

  std::cout << "capture-view setup wizard\n";
  std::cout << "Goal: lowest practical latency for console/gameplay preview.\n";
  std::cout << "Best latency: raw YUYV/NV12 if USB bandwidth holds, ultra latency, immediate pacing,\n";
  std::cout << "nearest/bilinear upscale. Bilinear+RCAS improves fullscreen sharpness with a small GPU pass.\n";

  const auto devices = list_video_devices();
  if (devices.empty()) {
    throw AppError("no V4L2 video capture devices found");
  }

  std::cout << "\nVideo devices\n";
  for (size_t i = 0; i < devices.size(); ++i) {
    std::cout << "  " << (i + 1) << ". " << devices[i].path << "  " << devices[i].card;
    if (devices[i].path == "/dev/video2" || i == 0) {
      std::cout << "  [suggested]";
    }
    std::cout << "\n";
  }
  const auto& device = devices[prompt_index(devices.size(), 0)];
  options.video_device = device.path;
  log::info("wizard video=", options.video_device);

  auto modes = collect_modes(device);
  if (modes.empty()) {
    throw AppError("selected video device has no supported MJPEG/YUYV/NV12 modes");
  }
  if (modes.size() > 8) {
    modes.resize(8);
  }

  std::cout << "\nVideo modes\n";
  for (size_t i = 0; i < modes.size(); ++i) {
    const auto& mode = modes[i];
    std::cout << "  " << (i + 1) << ". " << mode.size.width << "x" << mode.size.height
              << " " << mode.fps << "fps " << to_string(mode.format);
    if (i == 0) {
      std::cout << "  [suggested: " << mode.reason << "]";
    }
    std::cout << "\n";
  }
  const auto& mode = modes[prompt_index(modes.size(), 0)];
  options.size = mode.size;
  options.fps = mode.fps;
  options.format = mode.format;
  log::info("wizard mode=", options.size.width, "x", options.size.height, " fps=", options.fps,
            " format=", to_string(options.format));

  std::cout << "\nLatency mode\n";
  std::cout << "  1. ultra     [suggested: no vsync, 2 video buffers, 20ms audio]\n";
  std::cout << "  2. low       [safer: no vsync, 3 video buffers, 30ms audio]\n";
  std::cout << "  3. balanced  [smooth: vsync, 3 video buffers, 40ms audio]\n";
  const size_t latency = prompt_index(3, 0);
  apply_latency(options, latency == 0 ? "ultra" : latency == 1 ? "low" : "balanced");

  options.audio_monitor = prompt_yes_no("Enable audio monitoring?", false);
  if (options.audio_monitor) {
    const auto audio_devices = list_audio_devices();
    options.audio_input = choose_audio(filter_audio(audio_devices, "Audio/Source"), "Audio input source");
    options.audio_output = choose_audio(filter_audio(audio_devices, "Audio/Sink"), "Audio output sink");
  }

  std::cout << "\nSelected\n";
  std::cout << "  video: " << options.video_device << "\n";
  std::cout << "  mode: " << options.size.width << "x" << options.size.height
            << " " << options.fps << "fps " << to_string(options.format) << "\n";
  std::cout << "  latency: " << options.latency_mode << " buffers=" << options.buffer_count
            << " vsync=" << (options.vsync ? "on" : "off") << "\n";
  if (options.audio_monitor) {
    std::cout << "  audio input: " << options.audio_input << "\n";
    std::cout << "  audio output: " << options.audio_output << "\n";
    std::cout << "  audio buffer: " << options.audio_buffer_ms << "ms\n";
  }
  std::cout << "\nStarting viewer. Press R to restart capture, S for stats, F fullscreen, Alt+B borderless.\n";
}

} // namespace cv
