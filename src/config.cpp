#include "config.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <random>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
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
#ifdef _WIN32
  if (const char* appdata = std::getenv("APPDATA"); appdata != nullptr && *appdata != '\0') {
    return std::filesystem::path(appdata) / "capture-view" / file_name;
  }
  if (const char* local_appdata = std::getenv("LOCALAPPDATA"); local_appdata != nullptr && *local_appdata != '\0') {
    return std::filesystem::path(local_appdata) / "capture-view" / file_name;
  }
#else
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg != '\0') {
    return std::filesystem::path(xdg) / "lowlat-capture-viewer" / file_name;
  }
#endif
  if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
    return std::filesystem::path(home) / ".config" / "lowlat-capture-viewer" / file_name;
  }
  return file_name;
}

std::filesystem::path config_dir() { return config_path().parent_path(); }

std::string trim(std::string value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return {};
  }
  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

std::string quote(const std::string& value) {
  std::string result = "\"";
  for (char c : value) {
    switch (c) {
    case '\\': result += "\\\\"; break;
    case '"': result += "\\\""; break;
    case '\n': result += "\\n"; break;
    case '\r': result += "\\r"; break;
    case '\t': result += "\\t"; break;
    default: result += c; break;
    }
  }
  return result + '"';
}

std::string unquote(std::string value) {
  value = trim(std::move(value));
  if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
    return value;
  }
  std::string result;
  for (size_t i = 1; i + 1 < value.size(); ++i) {
    if (value[i] == '\\' && i + 2 < value.size()) {
      switch (value[++i]) {
      case 'n': result += '\n'; break;
      case 'r': result += '\r'; break;
      case 't': result += '\t'; break;
      case '\\': result += '\\'; break;
      case '"': result += '"'; break;
      default: result += '\\'; result += value[i]; break;
      }
    } else {
      result += value[i];
    }
  }
  return result;
}

bool parse_bool(const std::string& value) { return value == "true" || value == "1" || value == "yes"; }

uint32_t parse_u32_or(std::string value, uint32_t fallback) {
  try {
    return static_cast<uint32_t>(std::stoul(value));
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

std::string config_file_path() { return config_path().string(); }

std::string config_directory_path() { return config_dir().string(); }

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
  write_if_missing("console", "video = \"/dev/video2\"\n"
                              "width = 1920\n"
                              "height = 1080\n"
                              "fps = 60\n"
                              "format = \"mjpeg\"\n"
                              "latency_mode = \"ultra\"\n"
                              "frame_pacing = \"immediate\"\n"
                              "render_backend = \"sdl\"\n"
                              "vsync = false\n"
                              "fullscreen = false\n"
                              "borderless = false\n"
                              "audio_monitor = true\n"
                              "audio_buffer_ms = 3\n"
                              "audio_autotune = true\n");
  write_if_missing("audio-test", "test_pattern = true\n"
                                 "audio_monitor = true\n"
                                 "audio_buffer_ms = 20\n"
                                 "audio_autotune = true\n");
  write_if_missing("safe", "video = \"/dev/video2\"\n"
                           "width = 1920\n"
                           "height = 1080\n"
                           "fps = 60\n"
                           "format = \"mjpeg\"\n"
                           "latency_mode = \"low\"\n"
                           "frame_pacing = \"yield\"\n"
                           "render_backend = \"sdl\"\n"
                           "vsync = false\n"
                           "buffer_count = 3\n"
                           "audio_monitor = true\n"
                           "audio_buffer_ms = 20\n"
                           "audio_autotune = true\n");
}

void set_config_location(const CliOptions& options) {
  g_config_override =
      options.config_file.empty() ? std::filesystem::path{} : std::filesystem::path(options.config_file);
  g_profile = options.profile;
}

void load_config(CliOptions& options) {
  if (options.no_config || options.no_load_config) {
    return;
  }

  std::ifstream file(config_path());
  if (!file) {
    return;
  }

  std::string line;
  while (std::getline(file, line)) {
    bool quoted = false;
    bool escaped = false;
    for (size_t i = 0; i < line.size(); ++i) {
      const char c = line[i];
      if (escaped) {
        escaped = false;
      } else if (quoted && c == '\\') {
        escaped = true;
      } else if (c == '"') {
        quoted = !quoted;
      } else if (c == '#' && !quoted) {
        line.resize(i);
        break;
      }
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

  const auto path = std::filesystem::weakly_canonical(config_path());
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path());
  }
  auto temporary = path;
  temporary += "." + std::to_string(std::random_device{}()) + ".tmp";
  std::ofstream file(temporary, std::ios::out | std::ios::noreplace);
  if (!file) {
    throw AppError("could not create temporary config: " + temporary.string());
  }
  try {
    file.exceptions(std::ios::failbit | std::ios::badbit);
    const auto permissions = std::filesystem::exists(path)
                                 ? std::filesystem::status(path).permissions()
                                 : std::filesystem::perms::owner_read | std::filesystem::perms::owner_write;
    std::filesystem::permissions(temporary, permissions);

    file << "video = " << quote(options.video_device) << "\n";
    file << "video_output = " << quote(options.video_output) << "\n";
    file << "video_output_format = " << quote(options.video_output_format) << "\n";
    file << "audio_input = " << quote(options.audio_input) << "\n";
    file << "audio_output = " << quote(options.audio_output) << "\n";
    file << "audio_virtual_source = " << quote(options.audio_virtual_source) << "\n";
    file << "width = " << options.size.width << "\n";
    file << "height = " << options.size.height << "\n";
    file << "fps = " << options.fps << "\n";
    file << "format = " << quote(to_string(options.format)) << "\n";
    file << "latency_mode = " << quote(options.latency_mode) << "\n";
    file << "frame_pacing = " << quote(options.frame_pacing) << "\n";
    file << "render_backend = " << quote(options.render_backend) << "\n";
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
    file.close();
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
      throw AppError("could not replace config: " + path.string());
    }
#else
    std::filesystem::rename(temporary, path);
#endif
  } catch (...) {
    if (file.is_open()) {
      file.exceptions(std::ios::goodbit);
      file.close();
    }
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    throw;
  }
}

} // namespace cv
