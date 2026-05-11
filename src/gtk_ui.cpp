#include "gtk_ui.hpp"

#include "audio_devices.hpp"
#include "common.hpp"
#include "v4l2_discovery.hpp"

#include <gtk/gtk.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>
#include <unistd.h>

namespace cv {

namespace {

struct GtkUiState {
  CliOptions options;
  std::vector<VideoDeviceInfo> video_devices;
  std::vector<AudioDeviceInfo> audio_inputs;
  std::vector<AudioDeviceInfo> audio_outputs;
  struct ModeCandidate {
    Size size;
    uint32_t fps = 0;
    PixelFormat format = PixelFormat::Unknown;
    int score = 0;
    std::string reason;
  };
  std::vector<ModeCandidate> modes;
  GtkWidget* video = nullptr;
  GtkWidget* mode = nullptr;
  GtkWidget* latency = nullptr;
  GtkWidget* pacing = nullptr;
  GtkWidget* scaling = nullptr;
  GtkWidget* upscale = nullptr;
  GtkWidget* audio_monitor = nullptr;
  GtkWidget* audio_input = nullptr;
  GtkWidget* audio_output = nullptr;
  GtkWidget* vsync = nullptr;
  GtkWidget* fullscreen = nullptr;
  GtkWidget* borderless = nullptr;
  GtkWidget* status = nullptr;
};

std::string current_executable() {
  std::array<char, 4096> buffer{};
  const ssize_t length = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
  if (length > 0) {
    return std::string(buffer.data(), static_cast<size_t>(length));
  }
  return "capture-view";
}

std::string combo_text(GtkWidget* widget) {
  GtkDropDown* dropdown = GTK_DROP_DOWN(widget);
  auto* item = static_cast<GObject*>(gtk_drop_down_get_selected_item(dropdown));
  if (item == nullptr) {
    return {};
  }
  return gtk_string_object_get_string(GTK_STRING_OBJECT(item));
}

guint selected_index(GtkWidget* widget) {
  const guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(widget));
  return selected == GTK_INVALID_LIST_POSITION ? 0 : selected;
}

void append_labeled(GtkGrid* grid, const char* label, GtkWidget* widget, int row) {
  GtkWidget* text = gtk_label_new(label);
  gtk_widget_set_halign(text, GTK_ALIGN_START);
  gtk_grid_attach(grid, text, 0, row, 1, 1);
  gtk_grid_attach(grid, widget, 1, row, 1, 1);
}

GtkWidget* combo_from_values(const std::vector<std::string>& values, const std::string& active) {
  GtkStringList* model = gtk_string_list_new(nullptr);
  int active_index = 0;
  for (size_t i = 0; i < values.size(); ++i) {
    gtk_string_list_append(model, values[i].c_str());
    if (values[i] == active) {
      active_index = static_cast<int>(i);
    }
  }
  GtkWidget* dropdown = gtk_drop_down_new(G_LIST_MODEL(model), nullptr);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(dropdown), static_cast<unsigned int>(active_index));
  return dropdown;
}

void replace_dropdown_values(GtkWidget* dropdown, const std::vector<std::string>& values) {
  GtkStringList* model = gtk_string_list_new(nullptr);
  for (const auto& value : values) {
    gtk_string_list_append(model, value.c_str());
  }
  gtk_drop_down_set_model(GTK_DROP_DOWN(dropdown), G_LIST_MODEL(model));
  gtk_drop_down_set_selected(GTK_DROP_DOWN(dropdown), 0);
}

std::string video_label(const VideoDeviceInfo& device) {
  std::string label = device.path;
  if (!device.card.empty()) {
    label += "  " + device.card;
  }
  return label;
}

std::vector<VideoDeviceInfo> discover_video_devices(const std::string& fallback) {
  std::vector<VideoDeviceInfo> devices;
  try {
    devices = list_video_devices();
  } catch (const AppError&) {
  }
  if (devices.empty()) {
    devices.push_back({fallback, {}, "manual device", {}, {}});
  }
  return devices;
}

std::vector<GtkUiState::ModeCandidate> collect_modes(const VideoDeviceInfo& device) {
  std::vector<GtkUiState::ModeCandidate> modes;
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
  if (modes.empty()) {
    modes.push_back({Size{1280, 720}, 60, PixelFormat::Auto, 0, "manual fallback"});
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
    mode.reason = mode.format == PixelFormat::Mjpeg ? "USB-friendly 1080p60" : "raw low decode latency";
  }

  std::ranges::sort(modes, [](const auto& a, const auto& b) {
    return a.score > b.score;
  });
  modes.erase(std::unique(modes.begin(), modes.end(), [](const auto& a, const auto& b) {
                return a.size.width == b.size.width && a.size.height == b.size.height &&
                       a.fps == b.fps && a.format == b.format;
              }),
              modes.end());
  if (modes.size() > 8) {
    modes.resize(8);
  }
  return modes;
}

std::string mode_label(const GtkUiState::ModeCandidate& mode) {
  return std::to_string(mode.size.width) + "x" + std::to_string(mode.size.height) + "  " +
         std::to_string(mode.fps) + "fps  " + to_string(mode.format) + "  " + mode.reason;
}

std::vector<std::string> mode_labels(const std::vector<GtkUiState::ModeCandidate>& modes) {
  std::vector<std::string> labels;
  labels.reserve(modes.size());
  for (const auto& mode : modes) {
    labels.push_back(mode_label(mode));
  }
  return labels;
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
  return std::to_string(device.id) + "  " + audio_device_display_name(device) + "  [" +
         audio_device_detail(device) + "]";
}

std::vector<std::string> audio_labels(const std::vector<AudioDeviceInfo>& devices) {
  std::vector<std::string> labels;
  labels.reserve(devices.size());
  for (const auto& device : devices) {
    labels.push_back(audio_label(device));
  }
  if (labels.empty()) {
    labels.push_back("No PipeWire node found");
  }
  return labels;
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

void set_status(GtkUiState* state, const std::string& text) {
  gtk_label_set_text(GTK_LABEL(state->status), text.c_str());
}

void video_changed(GtkDropDown*, GParamSpec*, gpointer user_data) {
  auto* state = static_cast<GtkUiState*>(user_data);
  const guint index = selected_index(state->video);
  if (index >= state->video_devices.size()) {
    return;
  }
  state->modes = collect_modes(state->video_devices[index]);
  replace_dropdown_values(state->mode, mode_labels(state->modes));
}

void start_viewer(GtkButton*, gpointer user_data) {
  auto* state = static_cast<GtkUiState*>(user_data);

  CliOptions options = state->options;
  const guint video_index = selected_index(state->video);
  if (video_index < state->video_devices.size()) {
    options.video_device = state->video_devices[video_index].path;
  }
  const guint mode_index = selected_index(state->mode);
  if (mode_index < state->modes.size()) {
    const auto& mode = state->modes[mode_index];
    options.size = mode.size;
    options.fps = mode.fps;
    options.format = mode.format;
  }
  const std::string latency = combo_text(state->latency);
  apply_latency(options, latency);
  const std::string pacing = combo_text(state->pacing);
  const std::string scaling = combo_text(state->scaling);
  const std::string upscale = combo_text(state->upscale);
  options.frame_pacing = pacing;
  options.output_scaling = scaling;
  options.upscale_quality = upscale;
  options.vsync = gtk_check_button_get_active(GTK_CHECK_BUTTON(state->vsync));
  options.fullscreen = gtk_check_button_get_active(GTK_CHECK_BUTTON(state->fullscreen));
  options.borderless = gtk_check_button_get_active(GTK_CHECK_BUTTON(state->borderless));
  options.audio_monitor = gtk_check_button_get_active(GTK_CHECK_BUTTON(state->audio_monitor));
  const guint audio_input_index = selected_index(state->audio_input);
  const guint audio_output_index = selected_index(state->audio_output);
  if (options.audio_monitor && audio_input_index < state->audio_inputs.size()) {
    options.audio_input = state->audio_inputs[audio_input_index].name;
  }
  if (options.audio_monitor && audio_output_index < state->audio_outputs.size()) {
    options.audio_output = state->audio_outputs[audio_output_index].name;
  }

  std::vector<std::string> args;
  args.push_back(current_executable());
  args.push_back("--video");
  args.push_back(options.video_device);
  args.push_back("--size");
  args.push_back(std::to_string(options.size.width) + "x" + std::to_string(options.size.height));
  args.push_back("--fps");
  args.push_back(std::to_string(options.fps));
  args.push_back("--format");
  args.push_back(to_string(options.format));
  args.push_back("--latency-mode");
  args.push_back(options.latency_mode);
  args.push_back("--frame-pacing");
  args.push_back(options.frame_pacing);
  args.push_back("--output-scaling");
  args.push_back(options.output_scaling);
  args.push_back("--upscale-quality");
  args.push_back(options.upscale_quality);
  args.push_back(options.vsync ? "--vsync" : "--no-vsync");
  if (options.fullscreen) {
    args.push_back("--fullscreen");
  }
  if (options.borderless) {
    args.push_back("--borderless");
  }
  if (options.audio_monitor) {
    args.push_back("--audio-monitor");
    if (!options.audio_input.empty()) {
      args.push_back("--audio-input");
      args.push_back(options.audio_input);
    }
    if (!options.audio_output.empty()) {
      args.push_back("--audio-output");
      args.push_back(options.audio_output);
    }
  }

  std::vector<char*> argv;
  argv.reserve(args.size() + 1);
  for (auto& arg : args) {
    argv.push_back(arg.data());
  }
  argv.push_back(nullptr);

  GError* error = nullptr;
  if (!g_spawn_async(nullptr, argv.data(), nullptr, G_SPAWN_SEARCH_PATH, nullptr, nullptr, nullptr, &error)) {
    std::string message = "launch failed";
    if (error != nullptr) {
      message += ": ";
      message += error->message;
      g_error_free(error);
    }
    set_status(state, message);
    return;
  }
  set_status(state, "viewer started");
}

void activate(GtkApplication* app, gpointer user_data) {
  auto* state = static_cast<GtkUiState*>(user_data);

  GtkWidget* window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), "capture-view");
  gtk_window_set_default_size(GTK_WINDOW(window), 640, 460);

  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(box, 16);
  gtk_widget_set_margin_bottom(box, 16);
  gtk_widget_set_margin_start(box, 16);
  gtk_widget_set_margin_end(box, 16);
  gtk_window_set_child(GTK_WINDOW(window), box);

  GtkWidget* title = gtk_label_new("Capture View");
  gtk_widget_add_css_class(title, "title-2");
  gtk_widget_set_halign(title, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(box), title);

  GtkWidget* grid_widget = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid_widget), 8);
  gtk_grid_set_column_spacing(GTK_GRID(grid_widget), 12);
  gtk_widget_set_hexpand(grid_widget, true);
  gtk_box_append(GTK_BOX(box), grid_widget);
  GtkGrid* grid = GTK_GRID(grid_widget);

  state->video_devices = discover_video_devices(state->options.video_device);
  std::vector<std::string> video_labels;
  video_labels.reserve(state->video_devices.size());
  for (const auto& device : state->video_devices) {
    video_labels.push_back(video_label(device));
  }
  state->modes = collect_modes(state->video_devices.front());
  try {
    const auto audio_devices = list_audio_devices();
    state->audio_inputs = filter_audio(audio_devices, "Audio/Source");
    state->audio_outputs = filter_audio(audio_devices, "Audio/Sink");
  } catch (const AppError&) {
  }

  state->video = combo_from_values(video_labels, video_labels.front());
  state->mode = combo_from_values(mode_labels(state->modes), mode_label(state->modes.front()));
  state->latency = combo_from_values({"ultra", "low", "balanced"},
                                     state->options.latency_mode.empty() ? "ultra" : state->options.latency_mode);
  state->pacing = combo_from_values({"immediate", "yield", "sleep", "adaptive"}, state->options.frame_pacing);
  state->scaling = combo_from_values({"fit", "fill", "stretch", "integer"}, state->options.output_scaling);
  state->upscale = combo_from_values({"linear", "nearest"}, state->options.upscale_quality);
  state->audio_input = combo_from_values(audio_labels(state->audio_inputs), {});
  state->audio_output = combo_from_values(audio_labels(state->audio_outputs), {});
  g_signal_connect(state->video, "notify::selected", G_CALLBACK(video_changed), state);

  append_labeled(grid, "Video device", state->video, 0);
  append_labeled(grid, "Video mode", state->mode, 1);
  append_labeled(grid, "Latency", state->latency, 2);
  append_labeled(grid, "Pacing", state->pacing, 3);
  append_labeled(grid, "Scaling", state->scaling, 4);
  append_labeled(grid, "Upscale", state->upscale, 5);

  state->audio_monitor = gtk_check_button_new_with_label("Audio monitor");
  gtk_check_button_set_active(GTK_CHECK_BUTTON(state->audio_monitor), state->options.audio_monitor);
  gtk_grid_attach(grid, state->audio_monitor, 1, 6, 1, 1);
  append_labeled(grid, "Audio input", state->audio_input, 7);
  append_labeled(grid, "Audio output", state->audio_output, 8);

  state->vsync = gtk_check_button_new_with_label("VSync");
  gtk_check_button_set_active(GTK_CHECK_BUTTON(state->vsync), state->options.vsync);
  gtk_grid_attach(grid, state->vsync, 1, 9, 1, 1);

  state->fullscreen = gtk_check_button_new_with_label("Fullscreen");
  gtk_check_button_set_active(GTK_CHECK_BUTTON(state->fullscreen), state->options.fullscreen);
  gtk_grid_attach(grid, state->fullscreen, 1, 10, 1, 1);

  state->borderless = gtk_check_button_new_with_label("Borderless");
  gtk_check_button_set_active(GTK_CHECK_BUTTON(state->borderless), state->options.borderless);
  gtk_grid_attach(grid, state->borderless, 1, 11, 1, 1);

  GtkWidget* button = gtk_button_new_with_label("Start Viewer");
  gtk_widget_set_halign(button, GTK_ALIGN_END);
  g_signal_connect(button, "clicked", G_CALLBACK(start_viewer), state);
  gtk_box_append(GTK_BOX(box), button);

  state->status = gtk_label_new("");
  gtk_widget_set_halign(state->status, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(box), state->status);

  gtk_window_present(GTK_WINDOW(window));
}

} // namespace

int run_gtk_ui(const CliOptions& options) {
  GtkUiState state{};
  state.options = options;
  GtkApplication* app = gtk_application_new("dev.capture-view.launcher", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(activate), &state);
  const int status = g_application_run(G_APPLICATION(app), 0, nullptr);
  g_object_unref(app);
  return status;
}

} // namespace cv
