#include "../src/gtk_ui.cpp"
#include "audio_selector.hpp"

#include <iostream>

namespace {

bool discovery_fails = false;
int discoveries = 0;
std::vector<cv::AudioDeviceInfo> audio_devices;

void require(bool condition, const char* message) {
  if (!condition) throw cv::AppError(message);
}

void test_saved_selections() {
  std::vector<cv::VideoDeviceInfo> video{{"/dev/video0", {}, {}, {}, {}}, {"/dev/video2", {}, {}, {}, {}}};
  const auto selected_video = [&] {
    return cv::preferred_index(video, [](const auto& device) { return device.path == "/dev/video2"; });
  };
  require(selected_video() == 1, "saved video device was not selected");
  std::ranges::reverse(video);
  require(selected_video() == 0, "video selection depended on discovery order");

  std::vector<cv::AudioDeviceInfo> audio(2);
  audio[0].name = "microphone";
  audio[1].name = "capture-card";
  audio[1].id = 42;
  const auto selected_audio = [&] {
    return cv::preferred_index(audio, [](const auto& device) { return device.name == "capture-card"; });
  };
  require(selected_audio() == 1, "saved audio route was not selected");
  std::ranges::reverse(audio);
  audio[0].id = 99;
  require(selected_audio() == 0, "audio selection depended on a temporary PipeWire ID");

  cv::VideoDeviceInfo device;
  cv::FormatInfo format;
  format.fourcc = cv::pixel_format_to_v4l2(cv::PixelFormat::Yuyv);
  for (uint32_t width = 320; width <= 3200; width += 320) {
    format.sizes.push_back({{width, 240}, {{1, 60}}});
  }
  device.formats.push_back(format);
  const auto modes = cv::collect_modes(device);
  cv::CliOptions options;
  options.size = {320, 240};
  options.format = cv::PixelFormat::Yuyv;
  const auto mode = cv::preferred_mode(modes, options);
  require(modes.size() == 10 && mode >= 8 && modes[mode].size.width == 320,
          "saved mode outside the top eight was lost");
  options.size = {1, 1};
  require(cv::preferred_mode(modes, options) == 0, "missing mode did not fall back to the first choice");
}

void test_optional_audio_discovery() {
  for (bool fail : {true, false}) {
    discovery_fails = fail;
    cv::CliOptions options;
    options.audio_monitor = true;
    require(!cv::select_audio_devices_if_needed(options), "failed or empty discovery was treated as usable audio");
    require(options.audio_monitor, "temporary audio failure changed the saved monitoring preference");
  }
  audio_devices.resize(1);
  audio_devices[0].media_class = "Audio/Sink";
  cv::CliOptions options;
  options.audio_monitor = true;
  options.audio_output = "headphones";
  require(!cv::select_audio_devices_if_needed(options), "missing capture source was treated as usable audio");

  options.audio_monitor = true;
  options.audio_test_tone = true;
  const int before = discoveries;
  require(cv::select_audio_devices_if_needed(options) && discoveries == before,
          "test tone unnecessarily required an input source");
  options.audio_test_tone = false;
  options.audio_input = "capture-card";
  require(cv::select_audio_devices_if_needed(options) && discoveries == before,
          "explicit audio routes triggered discovery");
}

} // namespace

namespace cv {

std::vector<AudioDeviceInfo> list_audio_devices() {
  ++discoveries;
  if (discovery_fails) throw AppError("test PipeWire unavailable");
  return audio_devices;
}

std::vector<VideoDeviceInfo> list_video_devices() { return {}; }
std::string audio_device_display_name(const AudioDeviceInfo& device) { return device.name; }
std::string audio_device_detail(const AudioDeviceInfo&) { return {}; }

} // namespace cv

int main() {
  try {
    test_saved_selections();
    test_optional_audio_discovery();
  } catch (const std::exception& error) {
    std::cerr << "test failed: " << error.what() << '\n';
    return 1;
  }
}
