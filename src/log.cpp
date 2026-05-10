#include "log.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>

namespace cv::log {

namespace {

std::mutex g_mutex;
std::ofstream g_file;
bool g_verbose = false;

const char* level_name(Level level) {
  switch (level) {
  case Level::Info:
    return "INFO";
  case Level::Warning:
    return "WARN";
  case Level::Error:
    return "ERROR";
  case Level::Debug:
    return "DEBUG";
  }
  return "INFO";
}

std::string timestamp() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t time = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  localtime_r(&time, &tm);
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
  return out.str();
}

} // namespace

void init(const std::string& file_path, bool verbose) {
  std::lock_guard lock(g_mutex);
  g_verbose = verbose;
  if (!file_path.empty()) {
    g_file.open(file_path, std::ios::app);
    if (!g_file) {
      std::cerr << timestamp() << " ERROR could not open log file: " << file_path << "\n";
    } else {
      g_file << timestamp() << " INFO log file opened: " << file_path << "\n";
    }
  }
}

void write(Level level, const std::string& message) {
  if (level == Level::Debug && !g_verbose) {
    return;
  }

  const std::string line = timestamp() + " " + level_name(level) + " " + message;
  std::lock_guard lock(g_mutex);
  std::cerr << line << "\n";
  if (g_file) {
    g_file << line << "\n";
    g_file.flush();
  }
}

} // namespace cv::log
