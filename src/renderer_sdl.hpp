#pragma once

#include "frame.hpp"

#include <SDL3/SDL.h>
#include <string>
#include <vector>

namespace cv {

enum class OutputScaling {
  Fit,
  Fill,
  Stretch,
  Integer,
};

enum class UpscaleQuality {
  Nearest,
  Linear,
};

struct RenderStats {
  double upload_ms = 0.0;
  double present_ms = 0.0;
};

class SdlRenderer {
public:
  SdlRenderer(std::string title, Size size, bool fullscreen, bool borderless, bool vsync, OutputScaling scaling,
              UpscaleQuality upscale_quality);
  ~SdlRenderer();

  SdlRenderer(const SdlRenderer&) = delete;
  SdlRenderer& operator=(const SdlRenderer&) = delete;

  bool handle_events(bool& restart_requested, bool& audio_restart_requested, bool& mute_requested,
                     float& volume_delta, bool& scaling_requested);
  void render(const RgbaFrame& frame, const std::string& stats_text);
  void render(FrameView frame, const std::string& stats_text);
  void set_gui_lines(std::vector<std::string> lines);
  void set_vsync(bool enabled);

  [[nodiscard]] bool show_stats() const { return show_stats_; }
  [[nodiscard]] bool vsync() const { return vsync_; }
  [[nodiscard]] bool fullscreen() const { return fullscreen_; }
  [[nodiscard]] bool borderless() const { return borderless_; }
  [[nodiscard]] OutputScaling scaling() const { return scaling_; }
  [[nodiscard]] RenderStats last_stats() const { return stats_; }

private:
  void ensure_texture(Size size, SDL_PixelFormat format);
  void render_texture(Size frame_size, const std::string& stats_text);
  void toggle_fullscreen();
  void toggle_borderless();
  void cycle_scaling();
  void show_cursor();
  void update_cursor_visibility();
  [[nodiscard]] SDL_FRect destination_rect(Size frame_size) const;

  SDL_Window* window_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
  SDL_Texture* texture_ = nullptr;
  Size texture_size_{};
  SDL_PixelFormat texture_format_ = SDL_PIXELFORMAT_UNKNOWN;
  bool fullscreen_ = false;
  bool borderless_ = false;
  bool show_stats_ = true;
  bool show_gui_ = false;
  bool vsync_ = false;
  bool window_focused_ = true;
  bool cursor_visible_ = true;
  Clock::time_point last_mouse_motion_ = Clock::now();
  OutputScaling scaling_ = OutputScaling::Fit;
  UpscaleQuality upscale_quality_ = UpscaleQuality::Linear;
  RenderStats stats_{};
  std::vector<std::string> gui_lines_;
};

OutputScaling output_scaling_from_string(const std::string& value);
std::string to_string(OutputScaling scaling);
UpscaleQuality upscale_quality_from_string(const std::string& value);
std::string to_string(UpscaleQuality quality);

} // namespace cv
