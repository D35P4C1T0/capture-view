#include "config.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace cv {

namespace {

std::filesystem::path g_config_override;
std::string g_profile;

std::filesystem::path config_path() {
  if (!g_config_override.empty()) {
    return g_config_override;
  }
  const std::string file_name = g_profile.empty() ? "config.toml" : g_profile + ".toml";
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg != '\0') {
    return std::filesystem::path(xdg) / "lowlat-capture-viewer" / file_name;
  }
  if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
    return std::filesystem::path(home) / ".config" / "lowlat-capture-viewer" / file_name;
  }
  return file_name;
}

std::filesystem::path config_dir() {
  return config_path().parent_path();
}

std::string trim(std::string value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return {};
  }
  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

std::string unquote(std::string value) {
  value = trim(std::move(value));
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    return value.substr(1, value.size() - 2);
  }
  return value;
}

bool parse_bool(const std::string& value) {
  return value == "true" || value == "1" || value == "yes";
}

uint32_t parse_u32_or(std::string value, uint32_t fallback) {
  try {
    return static_cast<uint32_t>(std::stoul(value));
  } catch (...) {
    return fallback;
  }
}

float parse_float_or(std::string value, float fallback) {
  try {
    return std::stof(value);
  } catch (...) {
    return fallback;
  }
}

int32_t parse_i32_or(std::string value, int32_t fallback) {
  try {
    return static_cast<int32_t>(std::stol(value));
  } catch (...) {
    return fallback;
  }
}

} // namespace

std::string config_file_path() {
  return config_path().string();
}

std::string config_directory_path() {
  return config_dir().string();
}

std::vector<std::string> list_config_profiles() {
  std::vector<std::string> profiles;
  const auto dir = config_dir();
  if (!std::filesystem::exists(dir)) {
    return profiles;
  }
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".toml") {
      continue;
    }
    profiles.push_back(entry.path().stem().string());
  }
  std::ranges::sort(profiles);
  return profiles;
}

void init_default_profiles() {
  const auto dir = config_dir();
  std::filesystem::create_directories(dir);
  const auto write_if_missing = [&](const std::string& name, const std::string& body) {
    const auto path = dir / (name + ".toml");
    if (std::filesystem::exists(path)) {
      return;
    }
    std::ofstream file(path);
    file << body;
  };
  write_if_missing("console",
                   "video = \"/dev/video2\"\n"
                   "width = 1920\n"
                   "height = 1080\n"
                   "fps = 60\n"
                   "format = \"mjpeg\"\n"
                   "latency_mode = \"ultra\"\n"
                   "frame_pacing = \"immediate\"\n"
                   "render_backend = \"sdl\"\n"
                   "output_scaling = \"fit\"\n"
                   "upscale_quality = \"bilinear\"\n"
                   "rcas_strength = 0.35\n"
                   "vsync = false\n"
                   "fullscreen = false\n"
                   "borderless = false\n"
                   "audio_monitor = true\n"
                   "audio_buffer_ms = 10\n"
                   "audio_autotune = true\n");
  write_if_missing("audio-test",
                   "test_pattern = true\n"
                   "audio_monitor = true\n"
                   "audio_buffer_ms = 20\n"
                   "audio_autotune = true\n");
  write_if_missing("safe",
                   "video = \"/dev/video2\"\n"
                   "width = 1920\n"
                   "height = 1080\n"
                   "fps = 60\n"
                   "format = \"mjpeg\"\n"
                   "latency_mode = \"low\"\n"
                   "frame_pacing = \"yield\"\n"
                   "render_backend = \"sdl\"\n"
                   "output_scaling = \"fit\"\n"
                   "upscale_quality = \"bilinear\"\n"
                   "rcas_strength = 0.35\n"
                   "vsync = false\n"
                   "buffer_count = 3\n"
                   "audio_monitor = true\n"
                   "audio_buffer_ms = 20\n"
                   "audio_autotune = true\n");
}

void set_config_location(const CliOptions& options) {
  g_config_override = options.config_file.empty() ? std::filesystem::path{} : std::filesystem::path(options.config_file);
  g_profile = options.profile;
}

void load_config(CliOptions& options) {
  if (options.no_config) {
    return;
  }

  std::ifstream file(config_path());
  if (!file) {
    return;
  }

  std::string line;
  while (std::getline(file, line)) {
    const auto comment = line.find('#');
    if (comment != std::string::npos) {
      line.resize(comment);
    }
    const auto eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    const std::string key = trim(line.substr(0, eq));
    const std::string value = trim(line.substr(eq + 1));

    if (key == "video") {
      options.video_device = unquote(value);
    } else if (key == "video_output") {
      options.video_output = unquote(value);
    } else if (key == "video_output_format") {
      const std::string parsed = unquote(value);
      if (parsed == "yuyv" || parsed == "nv12" || parsed == "rgba" || parsed == "mjpeg") {
        options.video_output_format = parsed;
      }
    } else if (key == "audio_input") {
      options.audio_input = unquote(value);
    } else if (key == "audio_output") {
      options.audio_output = unquote(value);
    } else if (key == "audio_virtual_source") {
      options.audio_virtual_source = unquote(value);
    } else if (key == "width") {
      options.size.width = parse_u32_or(value, options.size.width);
    } else if (key == "height") {
      options.size.height = parse_u32_or(value, options.size.height);
    } else if (key == "fps") {
      options.fps = parse_u32_or(value, options.fps);
    } else if (key == "format") {
      const PixelFormat parsed = pixel_format_from_cli(unquote(value));
      if (parsed != PixelFormat::Unknown) {
        options.format = parsed;
      }
    } else if (key == "latency_mode") {
      options.latency_mode = unquote(value);
    } else if (key == "frame_pacing") {
      const std::string parsed = unquote(value);
      if (parsed == "immediate" || parsed == "yield" || parsed == "sleep" || parsed == "adaptive") {
        options.frame_pacing = parsed;
      }
    } else if (key == "render_backend") {
      const std::string parsed = unquote(value);
      if (parsed == "sdl" || parsed == "opengl") {
        options.render_backend = parsed;
      }
    } else if (key == "output_scaling") {
      options.output_scaling = unquote(value);
    } else if (key == "upscale_quality") {
      const std::string parsed = unquote(value);
      if (parsed == "nearest" || parsed == "bilinear" || parsed == "bilinear-rcas") {
        options.upscale_quality = parsed;
      } else if (parsed == "linear") {
        options.upscale_quality = "bilinear";
      } else if (parsed == "rcas") {
        options.upscale_quality = "bilinear-rcas";
      }
    } else if (key == "rcas_strength") {
      options.rcas_strength = std::clamp(parse_float_or(value, options.rcas_strength), 0.0F, 1.0F);
    } else if (key == "vsync") {
      options.vsync = parse_bool(value);
    } else if (key == "fullscreen") {
      options.fullscreen = parse_bool(value);
    } else if (key == "borderless") {
      options.borderless = parse_bool(value);
    } else if (key == "buffer_count") {
      options.buffer_count = parse_u32_or(value, options.buffer_count);
    } else if (key == "test_pattern") {
      options.test_pattern = parse_bool(value);
    } else if (key == "audio_buffer_ms") {
      options.audio_buffer_ms = parse_u32_or(value, options.audio_buffer_ms);
    } else if (key == "audio_quantum") {
      options.audio_quantum = parse_u32_or(value, options.audio_quantum);
    } else if (key == "audio_delay_ms") {
      options.audio_delay_ms = std::clamp(parse_i32_or(value, options.audio_delay_ms), -200, 200);
    } else if (key == "audio_monitor") {
      options.audio_monitor = parse_bool(value);
    } else if (key == "audio_autotune") {
      options.audio_autotune = parse_bool(value);
    } else if (key == "muted") {
      options.muted = parse_bool(value);
    } else if (key == "volume") {
      try {
        options.volume = std::stof(value);
      } catch (...) {
      }
    }
  }
}

void save_config(const CliOptions& options) {
  if (options.no_config) {
    return;
  }

  const auto path = config_path();
  std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path);
  if (!file) {
    return;
  }

  file << "video = \"" << options.video_device << "\"\n";
  file << "video_output = \"" << options.video_output << "\"\n";
  file << "video_output_format = \"" << options.video_output_format << "\"\n";
  file << "audio_input = \"" << options.audio_input << "\"\n";
  file << "audio_output = \"" << options.audio_output << "\"\n";
  file << "audio_virtual_source = \"" << options.audio_virtual_source << "\"\n";
  file << "width = " << options.size.width << "\n";
  file << "height = " << options.size.height << "\n";
  file << "fps = " << options.fps << "\n";
  file << "format = \"" << to_string(options.format) << "\"\n";
  file << "latency_mode = \"" << options.latency_mode << "\"\n";
  file << "frame_pacing = \"" << options.frame_pacing << "\"\n";
  file << "render_backend = \"" << options.render_backend << "\"\n";
  file << "output_scaling = \"" << options.output_scaling << "\"\n";
  file << "upscale_quality = \"" << options.upscale_quality << "\"\n";
  file << "rcas_strength = " << options.rcas_strength << "\n";
  file << "vsync = " << (options.vsync ? "true" : "false") << "\n";
  file << "fullscreen = " << (options.fullscreen ? "true" : "false") << "\n";
  file << "borderless = " << (options.borderless ? "true" : "false") << "\n";
  file << "buffer_count = " << options.buffer_count << "\n";
  file << "audio_buffer_ms = " << options.audio_buffer_ms << "\n";
  file << "audio_quantum = " << options.audio_quantum << "\n";
  file << "audio_delay_ms = " << options.audio_delay_ms << "\n";
  file << "audio_monitor = " << (options.audio_monitor ? "true" : "false") << "\n";
  file << "audio_autotune = " << (options.audio_autotune ? "true" : "false") << "\n";
  file << "muted = " << (options.muted ? "true" : "false") << "\n";
  file << "volume = " << options.volume << "\n";
}

} // namespace cv
