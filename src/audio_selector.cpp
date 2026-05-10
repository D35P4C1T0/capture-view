#include "audio_selector.hpp"

#include "audio_devices.hpp"
#include "common.hpp"
#include "log.hpp"

#include <iostream>
#include <limits>
#include <string>
#include <unistd.h>
#include <vector>

namespace cv {

namespace {

std::vector<AudioDeviceInfo> filter_by_class(const std::vector<AudioDeviceInfo>& devices,
                                             const std::string& media_class) {
  std::vector<AudioDeviceInfo> filtered;
  for (const auto& device : devices) {
    if (device.media_class == media_class) {
      filtered.push_back(device);
    }
  }
  return filtered;
}

std::string label_for(const AudioDeviceInfo& device) {
  std::string label = std::to_string(device.id) + "  " + audio_device_display_name(device);
  label += "  [" + audio_device_detail(device) + "]";
  return label;
}

std::string choose_device(const std::vector<AudioDeviceInfo>& devices, const std::string& title) {
  if (devices.empty()) {
    throw AppError("no PipeWire " + title + " nodes found");
  }
  if (!::isatty(STDIN_FILENO)) {
    log::warning("audio selector skipped: stdin is not a terminal");
    return {};
  }

  std::cout << "\n" << title << "\n";
  for (size_t i = 0; i < devices.size(); ++i) {
    std::cout << "  " << (i + 1) << ". " << label_for(devices[i]) << "\n";
  }
  std::cout << "Select " << title << " [1-" << devices.size() << ", Enter=1]: " << std::flush;

  std::string line;
  std::getline(std::cin, line);
  if (line.empty()) {
    return devices.front().name;
  }

  size_t index = 0;
  try {
    index = static_cast<size_t>(std::stoul(line));
  } catch (...) {
    throw AppError("invalid audio selection: " + line);
  }
  if (index == 0 || index > devices.size()) {
    throw AppError("audio selection out of range: " + line);
  }
  return devices[index - 1].name;
}

} // namespace

void select_audio_devices_if_needed(CliOptions& options) {
  if (!options.audio_monitor) {
    return;
  }
  if (!options.audio_input.empty() && !options.audio_output.empty()) {
    return;
  }
  if (options.audio_test_tone && !options.audio_output.empty()) {
    return;
  }

  const auto devices = list_audio_devices();
  const auto sources = filter_by_class(devices, "Audio/Source");
  const auto sinks = filter_by_class(devices, "Audio/Sink");

  if (!options.audio_test_tone && options.audio_input.empty()) {
    options.audio_input = choose_device(sources, "Audio input source");
    log::info("audio selector input=", options.audio_input);
  }
  if (options.audio_output.empty()) {
    options.audio_output = choose_device(sinks, "Audio output sink");
    log::info("audio selector output=", options.audio_output);
  }
}

} // namespace cv
