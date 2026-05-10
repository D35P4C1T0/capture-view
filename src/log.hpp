#pragma once

#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>

namespace cv::log {

enum class Level {
  Info,
  Warning,
  Error,
  Debug,
};

void init(const std::string& file_path, bool verbose);
void write(Level level, const std::string& message);

template <typename... Args>
void message(Level level, Args&&... args) {
  std::ostringstream out;
  (out << ... << args);
  write(level, out.str());
}

template <typename... Args>
void info(Args&&... args) {
  message(Level::Info, std::forward<Args>(args)...);
}

template <typename... Args>
void warning(Args&&... args) {
  message(Level::Warning, std::forward<Args>(args)...);
}

template <typename... Args>
void error(Args&&... args) {
  message(Level::Error, std::forward<Args>(args)...);
}

template <typename... Args>
void debug(Args&&... args) {
  message(Level::Debug, std::forward<Args>(args)...);
}

} // namespace cv::log
