#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cv {

struct AudioDeviceInfo {
  uint32_t id = 0;
  std::string name;
  std::string nick;
  std::string description;
  std::string device_name;
  std::string product_name;
  std::string vendor_name;
  std::string audio_rate;
  std::string audio_channels;
  std::string audio_format;
  std::string media_class;
};

std::vector<AudioDeviceInfo> list_audio_devices();
std::string audio_device_display_name(const AudioDeviceInfo& device);
std::string audio_device_detail(const AudioDeviceInfo& device);
void print_audio_devices(const std::vector<AudioDeviceInfo>& devices);

} // namespace cv
