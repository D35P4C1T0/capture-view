#include "renderer_sdl.hpp"

#include "common.hpp"
#include "log.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace cv {

namespace {

double elapsed_ms(Clock::time_point start, Clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

} // namespace

SdlRenderer::SdlRenderer(std::string title,
                         Size size,
                         bool fullscreen,
                         bool borderless,
                         bool vsync,
                         OutputScaling scaling,
                         UpscaleQuality upscale_quality,
                         bool force_opengl)
    : fullscreen_(fullscreen),
      borderless_(borderless),
      vsync_(vsync),
      scaling_(scaling),
      upscale_quality_(upscale_quality == UpscaleQuality::BilinearRcas ? UpscaleQuality::Bilinear : upscale_quality) {
  if (force_opengl) {
    log::warning("OpenGL renderer is not available in the Windows build; using SDL renderer");
  }
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    throw AppError(std::string("SDL_Init failed: ") + SDL_GetError());
  }

  Uint32 flags = SDL_WINDOW_RESIZABLE;
  window_ = SDL_CreateWindow(title.c_str(), static_cast<int>(size.width), static_cast<int>(size.height), flags);
  if (window_ == nullptr) {
    throw AppError(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
  }
  if (borderless_) {
    SDL_SetWindowBordered(window_, false);
  }
  if (fullscreen_) {
    SDL_SetWindowFullscreen(window_, true);
  }

  renderer_ = SDL_CreateRenderer(window_, nullptr);
  if (renderer_ == nullptr) {
    throw AppError(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
  }
  set_vsync(vsync_);
}

SdlRenderer::~SdlRenderer() {
  if (texture_ != nullptr) {
    SDL_DestroyTexture(texture_);
  }
  if (renderer_ != nullptr) {
    SDL_DestroyRenderer(renderer_);
  }
  if (window_ != nullptr) {
    SDL_DestroyWindow(window_);
  }
  SDL_Quit();
}

bool SdlRenderer::handle_events(bool& restart_requested,
                                bool& audio_restart_requested,
                                bool& mute_requested,
                                float& volume_delta,
                                bool& scaling_requested) {
  restart_requested = false;
  audio_restart_requested = false;
  mute_requested = false;
  volume_delta = 0.0F;
  scaling_requested = false;

  SDL_Event event{};
  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_EVENT_QUIT) {
      return false;
    }
    if (event.type != SDL_EVENT_KEY_DOWN) {
      continue;
    }
    switch (event.key.key) {
    case SDLK_Q:
      return false;
    case SDLK_ESCAPE:
      if (fullscreen_) {
        toggle_fullscreen();
      } else {
        return false;
      }
      break;
    case SDLK_F:
      toggle_fullscreen();
      break;
    case SDLK_V:
      set_vsync(!vsync_);
      break;
    case SDLK_O:
      cycle_scaling();
      scaling_requested = true;
      break;
    case SDLK_U:
      cycle_upscale_quality();
      break;
    case SDLK_S:
      show_stats_ = !show_stats_;
      break;
    case SDLK_G:
      show_gui_ = !show_gui_;
      break;
    default:
      break;
    }
  }
  return true;
}

void SdlRenderer::render(const RgbaFrame& frame, const std::string& stats_text) {
  ensure_texture(frame.size, SDL_PIXELFORMAT_RGBA32);
  const auto upload_start = Clock::now();
  if (!SDL_UpdateTexture(texture_, nullptr, frame.pixels.data(), static_cast<int>(frame.size.width * 4))) {
    throw AppError(std::string("SDL_UpdateTexture failed: ") + SDL_GetError());
  }
  const auto upload_end = Clock::now();
  stats_.upload_ms = elapsed_ms(upload_start, upload_end);
  render_texture(frame.size, stats_text);
}

void SdlRenderer::render(FrameView frame, const std::string& stats_text) {
  if (frame.format == PixelFormat::Yuyv) {
    convert_yuyv_to_rgba(frame, gl_rgba_);
  } else if (frame.format == PixelFormat::Nv12) {
    convert_nv12_to_rgba(frame, gl_rgba_);
  } else {
    throw AppError("Windows SDL renderer supports RGBA, YUYV, and NV12 frames");
  }
  render(gl_rgba_, stats_text);
}

void SdlRenderer::set_gui_lines(std::vector<std::string> lines) {
  gui_lines_ = std::move(lines);
}

void SdlRenderer::set_vsync(bool enabled) {
  vsync_ = enabled;
  if (renderer_ != nullptr) {
    SDL_SetRenderVSync(renderer_, enabled ? 1 : 0);
  }
}

void SdlRenderer::set_rcas_strength(float strength) {
  rcas_strength_ = std::clamp(strength, 0.0F, 1.0F);
}

void SdlRenderer::ensure_texture(Size size, SDL_PixelFormat format) {
  if (texture_ != nullptr && texture_size_.width == size.width && texture_size_.height == size.height &&
      texture_format_ == format) {
    return;
  }
  if (texture_ != nullptr) {
    SDL_DestroyTexture(texture_);
  }
  texture_size_ = size;
  texture_format_ = format;
  texture_ = SDL_CreateTexture(renderer_,
                               format,
                               SDL_TEXTUREACCESS_STREAMING,
                               static_cast<int>(size.width),
                               static_cast<int>(size.height));
  if (texture_ == nullptr) {
    throw AppError(std::string("SDL_CreateTexture failed: ") + SDL_GetError());
  }
  const SDL_ScaleMode scale_mode = upscale_quality_ == UpscaleQuality::Nearest ? SDL_SCALEMODE_NEAREST
                                                                               : SDL_SCALEMODE_LINEAR;
  SDL_SetTextureScaleMode(texture_, scale_mode);
}

void SdlRenderer::render_texture(Size frame_size, const std::string&) {
  SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
  SDL_RenderClear(renderer_);
  const SDL_FRect dst = destination_rect(frame_size);
  SDL_RenderTexture(renderer_, texture_, nullptr, &dst);
  const auto present_start = Clock::now();
  SDL_RenderPresent(renderer_);
  const auto present_end = Clock::now();
  stats_.present_ms = elapsed_ms(present_start, present_end);
}

void SdlRenderer::cycle_upscale_quality() {
  if (upscale_quality_ == UpscaleQuality::Nearest) {
    upscale_quality_ = UpscaleQuality::Bilinear;
  } else {
    upscale_quality_ = UpscaleQuality::Nearest;
  }
}

void SdlRenderer::cycle_scaling() {
  if (scaling_ == OutputScaling::Fit) {
    scaling_ = OutputScaling::Fill;
  } else if (scaling_ == OutputScaling::Fill) {
    scaling_ = OutputScaling::Stretch;
  } else if (scaling_ == OutputScaling::Stretch) {
    scaling_ = OutputScaling::Integer;
  } else {
    scaling_ = OutputScaling::Fit;
  }
}

void SdlRenderer::toggle_fullscreen() {
  fullscreen_ = !fullscreen_;
  SDL_SetWindowFullscreen(window_, fullscreen_);
}

void SdlRenderer::toggle_borderless() {
  borderless_ = !borderless_;
  SDL_SetWindowBordered(window_, !borderless_);
}

SDL_FRect SdlRenderer::destination_rect(Size frame_size) const {
  int window_w = 0;
  int window_h = 0;
  SDL_GetWindowSizeInPixels(window_, &window_w, &window_h);
  if (window_w <= 0 || window_h <= 0 || scaling_ == OutputScaling::Stretch) {
    return SDL_FRect{0.0F, 0.0F, static_cast<float>(window_w), static_cast<float>(window_h)};
  }
  const float sx = static_cast<float>(window_w) / static_cast<float>(frame_size.width);
  const float sy = static_cast<float>(window_h) / static_cast<float>(frame_size.height);
  float scale = scaling_ == OutputScaling::Fit ? std::min(sx, sy) : std::max(sx, sy);
  if (scaling_ == OutputScaling::Integer) {
    scale = std::max(1.0F, std::floor(std::min(sx, sy)));
  }
  const float width = static_cast<float>(frame_size.width) * scale;
  const float height = static_cast<float>(frame_size.height) * scale;
  return SDL_FRect{(static_cast<float>(window_w) - width) * 0.5F,
                   (static_cast<float>(window_h) - height) * 0.5F,
                   width,
                   height};
}

OutputScaling output_scaling_from_string(const std::string& value) {
  if (value == "fill") {
    return OutputScaling::Fill;
  }
  if (value == "stretch") {
    return OutputScaling::Stretch;
  }
  if (value == "integer") {
    return OutputScaling::Integer;
  }
  return OutputScaling::Fit;
}

std::string to_string(OutputScaling scaling) {
  switch (scaling) {
  case OutputScaling::Fit:
    return "fit";
  case OutputScaling::Fill:
    return "fill";
  case OutputScaling::Stretch:
    return "stretch";
  case OutputScaling::Integer:
    return "integer";
  }
  return "fit";
}

UpscaleQuality upscale_quality_from_string(const std::string& value) {
  if (value == "nearest") {
    return UpscaleQuality::Nearest;
  }
  if (value == "bilinear-rcas") {
    return UpscaleQuality::BilinearRcas;
  }
  return UpscaleQuality::Bilinear;
}

std::string to_string(UpscaleQuality quality) {
  switch (quality) {
  case UpscaleQuality::Nearest:
    return "nearest";
  case UpscaleQuality::Bilinear:
    return "bilinear";
  case UpscaleQuality::BilinearRcas:
    return "bilinear-rcas";
  }
  return "bilinear";
}

} // namespace cv
