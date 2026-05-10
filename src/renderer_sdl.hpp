#pragma once

#include "frame.hpp"

#include <SDL3/SDL.h>
#include <string>

namespace cv {

enum class OutputScaling {
  Fit,
  Fill,
  Stretch,
  Integer,
};

struct RenderStats {
  double upload_ms = 0.0;
  double present_ms = 0.0;
};

class SdlRenderer {
public:
  SdlRenderer(std::string title, Size size, bool fullscreen, bool vsync, OutputScaling scaling);
  ~SdlRenderer();

  SdlRenderer(const SdlRenderer&) = delete;
  SdlRenderer& operator=(const SdlRenderer&) = delete;

  bool handle_events(bool& restart_requested, bool& audio_restart_requested, bool& mute_requested,
                     float& volume_delta, bool& scaling_requested);
  void render(const RgbaFrame& frame, const std::string& stats_text);
  void set_vsync(bool enabled);

  [[nodiscard]] bool show_stats() const { return show_stats_; }
  [[nodiscard]] bool vsync() const { return vsync_; }
  [[nodiscard]] bool fullscreen() const { return fullscreen_; }
  [[nodiscard]] OutputScaling scaling() const { return scaling_; }
  [[nodiscard]] RenderStats last_stats() const { return stats_; }

private:
  void ensure_texture(Size size);
  void toggle_fullscreen();
  void cycle_scaling();
  [[nodiscard]] SDL_FRect destination_rect(Size frame_size) const;

  SDL_Window* window_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
  SDL_Texture* texture_ = nullptr;
  Size texture_size_{};
  bool fullscreen_ = false;
  bool show_stats_ = true;
  bool vsync_ = false;
  OutputScaling scaling_ = OutputScaling::Fit;
  RenderStats stats_{};
};

OutputScaling output_scaling_from_string(const std::string& value);
std::string to_string(OutputScaling scaling);

} // namespace cv
