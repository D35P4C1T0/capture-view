#include "audio_devices.hpp"

#include "common.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <pipewire/pipewire.h>
#include <spa/utils/dict.h>

namespace cv {

namespace {

struct RegistryState {
  pw_main_loop* loop = nullptr;
  pw_core* core = nullptr;
  pw_registry* registry = nullptr;
  spa_hook registry_listener{};
  spa_hook core_listener{};
  int pending_sync = 0;
  bool done = false;
  std::vector<AudioDeviceInfo> devices;
};

const char* dict_lookup(const spa_dict* props, const char* key) {
  return props == nullptr ? nullptr : spa_dict_lookup(props, key);
}

std::string prop(const spa_dict* props, const char* key) {
  if (const char* value = dict_lookup(props, key)) {
    return value;
  }
  return {};
}

std::string cleaned(std::string value) {
  std::ranges::replace(value, '_', ' ');
  while (value.find("  ") != std::string::npos) {
    value.replace(value.find("  "), 2, " ");
  }
  return value;
}

void on_registry_global(void* data, uint32_t id, uint32_t, const char* type, uint32_t, const spa_dict* props) {
  auto* state = static_cast<RegistryState*>(data);
  if (std::string_view(type) != PW_TYPE_INTERFACE_Node) {
    return;
  }

  const char* media_class = dict_lookup(props, PW_KEY_MEDIA_CLASS);
  if (media_class == nullptr) {
    return;
  }
  const std::string_view klass(media_class);
  if (klass != "Audio/Source" && klass != "Audio/Sink") {
    return;
  }

  AudioDeviceInfo info;
  info.id = id;
  info.media_class = media_class;
  info.name = prop(props, PW_KEY_NODE_NAME);
  info.nick = prop(props, PW_KEY_NODE_NICK);
  info.description = prop(props, PW_KEY_NODE_DESCRIPTION);
  info.device_name = prop(props, PW_KEY_DEVICE_NAME);
  info.product_name = prop(props, PW_KEY_DEVICE_PRODUCT_NAME);
  info.vendor_name = prop(props, PW_KEY_DEVICE_VENDOR_NAME);
  info.audio_rate = prop(props, PW_KEY_AUDIO_RATE);
  info.audio_channels = prop(props, PW_KEY_AUDIO_CHANNELS);
  info.audio_format = prop(props, PW_KEY_AUDIO_FORMAT);
  state->devices.push_back(std::move(info));
}

void on_core_done(void* data, uint32_t id, int seq) {
  auto* state = static_cast<RegistryState*>(data);
  if (id == PW_ID_CORE && seq == state->pending_sync) {
    state->done = true;
    pw_main_loop_quit(state->loop);
  }
}

const pw_registry_events kRegistryEvents{
    .version = PW_VERSION_REGISTRY_EVENTS,
    .global = on_registry_global,
    .global_remove = nullptr,
};

const pw_core_events kCoreEvents{
    .version = PW_VERSION_CORE_EVENTS,
    .info = nullptr,
    .done = on_core_done,
    .ping = nullptr,
    .error = nullptr,
    .remove_id = nullptr,
    .bound_id = nullptr,
    .add_mem = nullptr,
    .remove_mem = nullptr,
    .bound_props = nullptr,
};

} // namespace

std::vector<AudioDeviceInfo> list_audio_devices() {
  pw_init(nullptr, nullptr);
  RegistryState state;
  state.loop = pw_main_loop_new(nullptr);
  if (state.loop == nullptr) {
    throw AppError("pw_main_loop_new failed");
  }

  pw_context* context = pw_context_new(pw_main_loop_get_loop(state.loop), nullptr, 0);
  if (context == nullptr) {
    pw_main_loop_destroy(state.loop);
    throw AppError("pw_context_new failed");
  }

  state.core = pw_context_connect(context, nullptr, 0);
  if (state.core == nullptr) {
    pw_context_destroy(context);
    pw_main_loop_destroy(state.loop);
    throw AppError("pw_context_connect failed");
  }

  pw_core_add_listener(state.core, &state.core_listener, &kCoreEvents, &state);
  state.registry = pw_core_get_registry(state.core, PW_VERSION_REGISTRY, 0);
  pw_registry_add_listener(state.registry, &state.registry_listener, &kRegistryEvents, &state);
  state.pending_sync = pw_core_sync(state.core, PW_ID_CORE, 0);
  pw_main_loop_run(state.loop);

  spa_hook_remove(&state.registry_listener);
  spa_hook_remove(&state.core_listener);
  pw_proxy_destroy(reinterpret_cast<pw_proxy*>(state.registry));
  pw_core_disconnect(state.core);
  pw_context_destroy(context);
  pw_main_loop_destroy(state.loop);
  return state.devices;
}

std::string audio_device_display_name(const AudioDeviceInfo& device) {
  if (!device.description.empty()) {
    return cleaned(device.description);
  }
  if (!device.product_name.empty() && !device.nick.empty()) {
    return cleaned(device.product_name + " - " + device.nick);
  }
  if (!device.product_name.empty()) {
    return cleaned(device.product_name);
  }
  if (!device.nick.empty()) {
    return cleaned(device.nick);
  }
  if (!device.device_name.empty()) {
    return cleaned(device.device_name);
  }
  return cleaned(device.name);
}

std::string audio_device_detail(const AudioDeviceInfo& device) {
  std::string detail = device.media_class == "Audio/Source" ? "input" : "output";
  if (!device.audio_channels.empty()) {
    detail += ", " + device.audio_channels + "ch";
  }
  if (!device.audio_rate.empty()) {
    detail += ", " + device.audio_rate + "Hz";
  }
  if (!device.audio_format.empty()) {
    detail += ", " + device.audio_format;
  }
  if (!device.product_name.empty() && device.description.find(device.product_name) == std::string::npos) {
    detail += ", " + cleaned(device.product_name);
  }
  return detail;
}

void print_audio_devices(const std::vector<AudioDeviceInfo>& devices) {
  if (devices.empty()) {
    std::cout << "No PipeWire audio source/sink nodes found.\n";
    return;
  }
  for (const auto& device : devices) {
    std::cout << device.id << "  " << audio_device_display_name(device)
              << "  [" << audio_device_detail(device) << "]\n"
              << "    node: " << device.name << "\n";
  }
}

} // namespace cv
