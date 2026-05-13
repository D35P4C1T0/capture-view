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
  Bilinear,
  BilinearRcas,
};

struct RenderStats {
  double upload_ms = 0.0;
  double present_ms = 0.0;
};

class SdlRenderer {
public:
  SdlRenderer(std::string title, Size size, bool fullscreen, bool borderless, bool vsync, OutputScaling scaling,
              UpscaleQuality upscale_quality, bool force_opengl);
  ~SdlRenderer();

  SdlRenderer(const SdlRenderer&) = delete;
  SdlRenderer& operator=(const SdlRenderer&) = delete;

  bool handle_events(bool& restart_requested, bool& audio_restart_requested, bool& mute_requested,
                     float& volume_delta, bool& scaling_requested);
  void render(const RgbaFrame& frame, const std::string& stats_text);
  void render(FrameView frame, const std::string& stats_text);
  void set_gui_lines(std::vector<std::string> lines);
  void set_vsync(bool enabled);
  void set_rcas_strength(float strength);

  [[nodiscard]] bool show_stats() const { return show_stats_; }
  [[nodiscard]] bool vsync() const { return vsync_; }
  [[nodiscard]] bool fullscreen() const { return fullscreen_; }
  [[nodiscard]] bool borderless() const { return borderless_; }
  [[nodiscard]] OutputScaling scaling() const { return scaling_; }
  [[nodiscard]] UpscaleQuality upscale_quality() const { return upscale_quality_; }
  [[nodiscard]] float rcas_strength() const { return rcas_strength_; }
  [[nodiscard]] bool opengl_backend() const { return gl_ready_; }
  [[nodiscard]] RenderStats last_stats() const { return stats_; }

private:
  void ensure_texture(Size size, SDL_PixelFormat format);
  void ensure_gl_texture(Size size);
  void ensure_gl_target(Size size);
  bool enable_gl_rcas();
  void render_texture(Size frame_size, const std::string& stats_text);
  void render_gl_texture(Size frame_size, const std::string& stats_text);
  void draw_gl_overlays(const std::string& stats_text, int window_w, int window_h);
  void toggle_fullscreen();
  void toggle_borderless();
  void cycle_upscale_quality();
  void cycle_scaling();
  void show_cursor();
  void update_cursor_visibility();
  void update_stats_title(const std::string& stats_text);
  [[nodiscard]] SDL_FRect destination_rect(Size frame_size) const;

  SDL_Window* window_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
  SDL_Texture* texture_ = nullptr;
  SDL_GLContext gl_context_ = nullptr;
  unsigned int gl_source_texture_ = 0;
  unsigned int gl_target_texture_ = 0;
  unsigned int gl_framebuffer_ = 0;
  unsigned int gl_video_program_ = 0;
  unsigned int gl_yuyv_program_ = 0;
  unsigned int gl_nv12_program_ = 0;
  unsigned int gl_rcas_program_ = 0;
  unsigned int gl_color_program_ = 0;
  unsigned int gl_vbo_ = 0;
  unsigned int gl_yuyv_texture_ = 0;
  unsigned int gl_nv12_y_texture_ = 0;
  unsigned int gl_nv12_uv_texture_ = 0;
  Size gl_yuyv_size_{};
  Size gl_nv12_size_{};
  Size texture_size_{};
  Size gl_target_size_{};
  SDL_PixelFormat texture_format_ = SDL_PIXELFORMAT_UNKNOWN;
  bool gl_ready_ = false;
  bool gl_warned_ = false;
  bool fullscreen_ = false;
  bool borderless_ = false;
  bool show_stats_ = true;
  bool show_gui_ = false;
  bool show_help_ = false;
  bool vsync_ = false;
  bool window_focused_ = true;
  bool cursor_visible_ = true;
  Clock::time_point last_mouse_motion_ = Clock::now();
  Clock::time_point last_title_update_ = {};
  OutputScaling scaling_ = OutputScaling::Fit;
  UpscaleQuality upscale_quality_ = UpscaleQuality::Bilinear;
  float rcas_strength_ = 0.35F;
  RenderStats stats_{};
  std::vector<std::string> gui_lines_;
  RgbaFrame gl_rgba_;
};

OutputScaling output_scaling_from_string(const std::string& value);
std::string to_string(OutputScaling scaling);
UpscaleQuality upscale_quality_from_string(const std::string& value);
std::string to_string(UpscaleQuality quality);

} // namespace cv
