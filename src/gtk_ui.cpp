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
#include <unistd.h>
#include <vector>

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
  GtkWidget* render_backend = nullptr;
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

guint preferred_index(const auto& values, auto matches) {
  const auto found = std::ranges::find_if(values, matches);
  return found == values.end() ? 0U : static_cast<guint>(found - values.begin());
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

void replace_dropdown_values(GtkWidget* dropdown, const std::vector<std::string>& values, guint active = 0) {
  GtkStringList* model = gtk_string_list_new(nullptr);
  for (const auto& value : values) {
    gtk_string_list_append(model, value.c_str());
  }
  gtk_drop_down_set_model(GTK_DROP_DOWN(dropdown), G_LIST_MODEL(model));
  g_object_unref(model);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(dropdown), active);
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
    const int format_score = mode.format == PixelFormat::Yuyv    ? 30
                             : mode.format == PixelFormat::Nv12  ? 25
                             : mode.format == PixelFormat::Mjpeg ? 20
                                                                 : 0;
    mode.score = (mode_1080p ? 10000 : 0) + (mode_720p ? 4000 : 0) + (mode_60 ? 3000 : 0) +
                 static_cast<int>(mode.size.width * mode.size.height / 1000) + static_cast<int>(mode.fps) * 10 +
                 format_score;
    mode.reason = mode.format == PixelFormat::H264    ? "USB-friendly; encoder latency varies"
                  : mode.format == PixelFormat::Mjpeg ? "USB-friendly 1080p60"
                                                      : "raw low decode latency";
  }

  std::ranges::sort(modes, [](const auto& a, const auto& b) { return a.score > b.score; });
  modes.erase(std::unique(modes.begin(), modes.end(),
                          [](const auto& a, const auto& b) {
                            return a.size.width == b.size.width && a.size.height == b.size.height && a.fps == b.fps &&
                                   a.format == b.format;
                          }),
              modes.end());
  return modes;
}

std::string mode_label(const GtkUiState::ModeCandidate& mode) {
  return std::to_string(mode.size.width) + "x" + std::to_string(mode.size.height) + "  " + std::to_string(mode.fps) +
         "fps  " + to_string(mode.format) + "  " + mode.reason;
}

std::vector<std::string> mode_labels(const std::vector<GtkUiState::ModeCandidate>& modes) {
  std::vector<std::string> labels;
  labels.reserve(modes.size());
  for (const auto& mode : modes) {
    labels.push_back(mode_label(mode));
  }
  return labels;
}

std::vector<AudioDeviceInfo> filter_audio(const std::vector<AudioDeviceInfo>& devices, const std::string& media_class) {
  std::vector<AudioDeviceInfo> out;
  for (const auto& device : devices) {
    if (device.media_class == media_class) {
      out.push_back(device);
    }
  }
  return out;
}

std::string audio_label(const AudioDeviceInfo& device) {
  return std::to_string(device.id) + "  " + audio_device_display_name(device) + "  [" + audio_device_detail(device) +
         "]";
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

guint preferred_mode(const std::vector<GtkUiState::ModeCandidate>& modes, const CliOptions& options) {
  return preferred_index(modes, [&](const auto& mode) {
    return mode.size.width == options.size.width && mode.size.height == options.size.height &&
           mode.fps == options.fps && mode.format == options.format;
  });
}

void remember_devices(GtkUiState* state) {
  auto& options = state->options;
  const guint video = selected_index(state->video);
  if (video < state->video_devices.size()) options.video_device = state->video_devices[video].path;
  const guint mode = selected_index(state->mode);
  if (mode < state->modes.size()) {
    options.size = state->modes[mode].size;
    options.fps = state->modes[mode].fps;
    options.format = state->modes[mode].format;
  }
  const guint input = selected_index(state->audio_input);
  if (input < state->audio_inputs.size()) options.audio_input = state->audio_inputs[input].name;
  const guint output = selected_index(state->audio_output);
  if (output < state->audio_outputs.size()) options.audio_output = state->audio_outputs[output].name;
}

void set_status(GtkUiState* state, const std::string& text) {
  gtk_label_set_text(GTK_LABEL(state->status), text.c_str());
}

void refresh_discovery(GtkUiState* state) {
  remember_devices(state);
  state->video_devices = discover_video_devices(state->options.video_device);
  std::vector<std::string> video_labels;
  video_labels.reserve(state->video_devices.size());
  for (const auto& device : state->video_devices) {
    video_labels.push_back(video_label(device));
  }
  const guint video = preferred_index(state->video_devices, [&](const auto& device) {
    return device.path == state->options.video_device;
  });
  replace_dropdown_values(state->video, video_labels, video);
  state->modes = collect_modes(state->video_devices[video]);
  replace_dropdown_values(state->mode, mode_labels(state->modes), preferred_mode(state->modes, state->options));

  state->audio_inputs.clear();
  state->audio_outputs.clear();
  try {
    const auto audio_devices = list_audio_devices();
    state->audio_inputs = filter_audio(audio_devices, "Audio/Source");
    state->audio_outputs = filter_audio(audio_devices, "Audio/Sink");
  } catch (const AppError&) {
  }
  replace_dropdown_values(state->audio_input, audio_labels(state->audio_inputs),
                          preferred_index(state->audio_inputs, [&](const auto& device) {
                            return device.name == state->options.audio_input;
                          }));
  replace_dropdown_values(state->audio_output, audio_labels(state->audio_outputs),
                          preferred_index(state->audio_outputs, [&](const auto& device) {
                            return device.name == state->options.audio_output;
                          }));

  set_status(state, "devices refreshed: " + std::to_string(state->video_devices.size()) + " video, " +
                        std::to_string(state->audio_inputs.size()) + " audio inputs, " +
                        std::to_string(state->audio_outputs.size()) + " audio outputs");
}

void video_changed(GtkDropDown*, GParamSpec*, gpointer user_data) {
  auto* state = static_cast<GtkUiState*>(user_data);
  const guint index = selected_index(state->video);
  if (index >= state->video_devices.size()) {
    return;
  }
  state->modes = collect_modes(state->video_devices[index]);
  replace_dropdown_values(state->mode, mode_labels(state->modes), preferred_mode(state->modes, state->options));
}

void start_viewer(GtkButton*, gpointer user_data) {
  auto* state = static_cast<GtkUiState*>(user_data);

  remember_devices(state);
  CliOptions options = state->options;
  const std::string latency = combo_text(state->latency);
  apply_latency_mode(options, latency);
  const std::string pacing = combo_text(state->pacing);
  const std::string render_backend = combo_text(state->render_backend);
  options.frame_pacing = pacing;
  options.render_backend = render_backend;
  options.vsync = gtk_check_button_get_active(GTK_CHECK_BUTTON(state->vsync));
  options.fullscreen = gtk_check_button_get_active(GTK_CHECK_BUTTON(state->fullscreen));
  options.borderless = gtk_check_button_get_active(GTK_CHECK_BUTTON(state->borderless));
  options.audio_monitor = gtk_check_button_get_active(GTK_CHECK_BUTTON(state->audio_monitor));
  auto args = viewer_arguments(options);
  args.insert(args.begin(), current_executable());

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

void refresh_clicked(GtkButton*, gpointer user_data) { refresh_discovery(static_cast<GtkUiState*>(user_data)); }

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

  GtkWidget* latency_hint = gtk_label_new("Best latency: raw YUYV/NV12 if USB allows, ultra latency, "
                                          "immediate pacing, and VSync off.");
  gtk_label_set_wrap(GTK_LABEL(latency_hint), true);
  gtk_widget_set_halign(latency_hint, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(box), latency_hint);

  GtkWidget* grid_widget = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid_widget), 8);
  gtk_grid_set_column_spacing(GTK_GRID(grid_widget), 12);
  gtk_widget_set_hexpand(grid_widget, true);
  gtk_box_append(GTK_BOX(box), grid_widget);
  GtkGrid* grid = GTK_GRID(grid_widget);

  state->video = combo_from_values({}, {});
  state->mode = combo_from_values({}, {});
  state->latency = combo_from_values({"ultra", "low", "balanced"},
                                     state->options.latency_mode.empty() ? "ultra" : state->options.latency_mode);
  state->pacing = combo_from_values({"immediate", "yield", "sleep", "adaptive"}, state->options.frame_pacing);
  state->render_backend = combo_from_values({"sdl", "opengl"}, state->options.render_backend);
  state->audio_input = combo_from_values({}, {});
  state->audio_output = combo_from_values({}, {});
  g_signal_connect(state->video, "notify::selected", G_CALLBACK(video_changed), state);

  append_labeled(grid, "Video device", state->video, 0);
  append_labeled(grid, "Video mode", state->mode, 1);
  append_labeled(grid, "Latency", state->latency, 2);
  append_labeled(grid, "Pacing", state->pacing, 3);
  append_labeled(grid, "Renderer", state->render_backend, 4);
  state->audio_monitor = gtk_check_button_new_with_label("Audio monitor");
  gtk_check_button_set_active(GTK_CHECK_BUTTON(state->audio_monitor), state->options.audio_monitor);
  gtk_grid_attach(grid, state->audio_monitor, 1, 5, 1, 1);
  append_labeled(grid, "Audio input", state->audio_input, 6);
  append_labeled(grid, "Audio output", state->audio_output, 7);

  state->vsync = gtk_check_button_new_with_label("VSync");
  gtk_check_button_set_active(GTK_CHECK_BUTTON(state->vsync), state->options.vsync);
  gtk_grid_attach(grid, state->vsync, 1, 8, 1, 1);

  state->fullscreen = gtk_check_button_new_with_label("Fullscreen");
  gtk_check_button_set_active(GTK_CHECK_BUTTON(state->fullscreen), state->options.fullscreen);
  gtk_grid_attach(grid, state->fullscreen, 1, 9, 1, 1);

  state->borderless = gtk_check_button_new_with_label("Borderless");
  gtk_check_button_set_active(GTK_CHECK_BUTTON(state->borderless), state->options.borderless);
  gtk_grid_attach(grid, state->borderless, 1, 10, 1, 1);

  GtkWidget* button = gtk_button_new_with_label("Start Viewer");
  gtk_widget_set_halign(button, GTK_ALIGN_END);
  g_signal_connect(button, "clicked", G_CALLBACK(start_viewer), state);
  GtkWidget* button_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(button_row, GTK_ALIGN_END);
  GtkWidget* refresh = gtk_button_new_with_label("Refresh Devices");
  g_signal_connect(refresh, "clicked", G_CALLBACK(refresh_clicked), state);
  gtk_box_append(GTK_BOX(button_row), refresh);
  gtk_box_append(GTK_BOX(button_row), button);
  gtk_box_append(GTK_BOX(box), button_row);

  state->status = gtk_label_new("");
  gtk_widget_set_halign(state->status, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(box), state->status);
  refresh_discovery(state);

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
