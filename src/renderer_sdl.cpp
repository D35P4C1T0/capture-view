#include "renderer_sdl.hpp"

#include <array>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>

namespace cv {

namespace {

double elapsed_ms(Clock::time_point start, Clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

using Glyph = std::array<const char*, 7>;

Glyph glyph_for(char c) {
  switch (c) {
  case '0': return {"111", "101", "101", "101", "101", "101", "111"};
  case '1': return {"010", "110", "010", "010", "010", "010", "111"};
  case '2': return {"111", "001", "001", "111", "100", "100", "111"};
  case '3': return {"111", "001", "001", "111", "001", "001", "111"};
  case '4': return {"101", "101", "101", "111", "001", "001", "001"};
  case '5': return {"111", "100", "100", "111", "001", "001", "111"};
  case '6': return {"111", "100", "100", "111", "101", "101", "111"};
  case '7': return {"111", "001", "001", "010", "010", "010", "010"};
  case '8': return {"111", "101", "101", "111", "101", "101", "111"};
  case '9': return {"111", "101", "101", "111", "001", "001", "111"};
  case 'a': return {"010", "101", "101", "111", "101", "101", "101"};
  case 'b': return {"110", "101", "101", "110", "101", "101", "110"};
  case 'c': return {"011", "100", "100", "100", "100", "100", "011"};
  case 'd': return {"110", "101", "101", "101", "101", "101", "110"};
  case 'e': return {"111", "100", "100", "110", "100", "100", "111"};
  case 'f': return {"111", "100", "100", "110", "100", "100", "100"};
  case 'g': return {"011", "100", "100", "101", "101", "101", "011"};
  case 'h': return {"101", "101", "101", "111", "101", "101", "101"};
  case 'i': return {"111", "010", "010", "010", "010", "010", "111"};
  case 'j': return {"001", "001", "001", "001", "101", "101", "010"};
  case 'k': return {"101", "101", "110", "100", "110", "101", "101"};
  case 'l': return {"100", "100", "100", "100", "100", "100", "111"};
  case 'm': return {"101", "111", "111", "101", "101", "101", "101"};
  case 'n': return {"101", "111", "111", "111", "111", "111", "101"};
  case 'o': return {"010", "101", "101", "101", "101", "101", "010"};
  case 'p': return {"110", "101", "101", "110", "100", "100", "100"};
  case 'q': return {"010", "101", "101", "101", "101", "011", "001"};
  case 'r': return {"110", "101", "101", "110", "110", "101", "101"};
  case 's': return {"011", "100", "100", "010", "001", "001", "110"};
  case 't': return {"111", "010", "010", "010", "010", "010", "010"};
  case 'u': return {"101", "101", "101", "101", "101", "101", "111"};
  case 'v': return {"101", "101", "101", "101", "101", "101", "010"};
  case 'w': return {"101", "101", "101", "101", "111", "111", "101"};
  case 'x': return {"101", "101", "101", "010", "101", "101", "101"};
  case 'y': return {"101", "101", "101", "010", "010", "010", "010"};
  case 'z': return {"111", "001", "001", "010", "100", "100", "111"};
  case '=': return {"000", "111", "000", "111", "000", "000", "000"};
  case '.': return {"000", "000", "000", "000", "000", "110", "110"};
  case '-': return {"000", "000", "000", "111", "000", "000", "000"};
  case '/': return {"001", "001", "010", "010", "100", "100", "000"};
  case ':': return {"000", "110", "110", "000", "110", "110", "000"};
  case ' ': return {"000", "000", "000", "000", "000", "000", "000"};
  default: return {"111", "001", "010", "010", "000", "010", "000"};
  }
}

void draw_text(SDL_Renderer* renderer, float x, float y, const std::string& text) {
  constexpr float scale = 2.0F;
  constexpr float advance = 8.0F;
  SDL_SetRenderDrawColor(renderer, 245, 245, 245, 255);
  for (char c : text) {
    const Glyph glyph = glyph_for(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    for (int row = 0; row < 7; ++row) {
      for (int col = 0; col < 3; ++col) {
        if (glyph[static_cast<size_t>(row)][col] != '1') {
          continue;
        }
        SDL_FRect rect{x + static_cast<float>(col) * scale,
                       y + static_cast<float>(row) * scale,
                       scale,
                       scale};
        SDL_RenderFillRect(renderer, &rect);
      }
    }
    x += advance;
  }
}

} // namespace

SdlRenderer::SdlRenderer(std::string title, Size size, bool fullscreen, bool vsync, OutputScaling scaling)
    : fullscreen_(fullscreen), vsync_(vsync), scaling_(scaling) {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    throw AppError(std::string("SDL_Init failed: ") + SDL_GetError());
  }

  Uint32 flags = SDL_WINDOW_RESIZABLE;
  if (fullscreen_) {
    flags |= SDL_WINDOW_FULLSCREEN;
  }

  window_ = SDL_CreateWindow(title.c_str(), static_cast<int>(size.width), static_cast<int>(size.height), flags);
  if (window_ == nullptr) {
    throw AppError(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
  }

  renderer_ = SDL_CreateRenderer(window_, nullptr);
  if (renderer_ == nullptr) {
    throw AppError(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
  }

  set_vsync(vsync_);
  ensure_texture(size);
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

bool SdlRenderer::handle_events(bool& restart_requested, bool& audio_restart_requested, bool& mute_requested,
                                float& volume_delta, bool& scaling_requested) {
  SDL_Event event{};
  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_EVENT_QUIT) {
      return false;
    }
    if (event.type != SDL_EVENT_KEY_DOWN) {
      continue;
    }
    const SDL_Keycode key = event.key.key;
    if (key == SDLK_Q) {
      return false;
    }
    if (key == SDLK_ESCAPE) {
      if (fullscreen_) {
        toggle_fullscreen();
      } else {
        return false;
      }
    } else if (key == SDLK_F) {
      toggle_fullscreen();
    } else if (key == SDLK_V) {
      set_vsync(!vsync_);
    } else if (key == SDLK_S) {
      show_stats_ = !show_stats_;
    } else if (key == SDLK_R) {
      restart_requested = true;
    } else if (key == SDLK_A) {
      audio_restart_requested = true;
    } else if (key == SDLK_M) {
      mute_requested = true;
    } else if (key == SDLK_O) {
      cycle_scaling();
      scaling_requested = true;
    } else if (key == SDLK_EQUALS || key == SDLK_PLUS) {
      volume_delta = 0.05F;
    } else if (key == SDLK_MINUS) {
      volume_delta = -0.05F;
    }
  }
  return true;
}

void SdlRenderer::render(const RgbaFrame& frame, const std::string& stats_text) {
  ensure_texture(frame.size);
  if (show_stats_) {
    SDL_SetWindowTitle(window_, stats_text.c_str());
  }

  const auto upload_start = Clock::now();
  if (!SDL_UpdateTexture(texture_, nullptr, frame.pixels.data(), static_cast<int>(frame.size.width * 4))) {
    throw AppError(std::string("SDL_UpdateTexture failed: ") + SDL_GetError());
  }
  const auto upload_end = Clock::now();

  SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
  SDL_RenderClear(renderer_);
  const SDL_FRect dst = destination_rect(frame.size);
  SDL_RenderTexture(renderer_, texture_, nullptr, &dst);

  if (show_stats_) {
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 180);
    SDL_FRect bg{8.0F, 8.0F, 760.0F, 24.0F};
    SDL_RenderFillRect(renderer_, &bg);
    draw_text(renderer_, 14.0F, 13.0F, stats_text);
  }

  const auto present_start = Clock::now();
  SDL_RenderPresent(renderer_);
  const auto present_end = Clock::now();

  stats_.upload_ms = elapsed_ms(upload_start, upload_end);
  stats_.present_ms = elapsed_ms(present_start, present_end);
}

void SdlRenderer::set_vsync(bool enabled) {
  vsync_ = enabled;
  SDL_SetRenderVSync(renderer_, enabled ? 1 : 0);
}

void SdlRenderer::ensure_texture(Size size) {
  if (texture_ != nullptr && texture_size_.width == size.width && texture_size_.height == size.height) {
    return;
  }
  if (texture_ != nullptr) {
    SDL_DestroyTexture(texture_);
  }
  texture_size_ = size;
  texture_ = SDL_CreateTexture(renderer_,
                               SDL_PIXELFORMAT_RGBA32,
                               SDL_TEXTUREACCESS_STREAMING,
                               static_cast<int>(size.width),
                               static_cast<int>(size.height));
  if (texture_ == nullptr) {
    throw AppError(std::string("SDL_CreateTexture failed: ") + SDL_GetError());
  }
}

void SdlRenderer::toggle_fullscreen() {
  fullscreen_ = !fullscreen_;
  SDL_SetWindowFullscreen(window_, fullscreen_);
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

SDL_FRect SdlRenderer::destination_rect(Size frame_size) const {
  int window_w = 0;
  int window_h = 0;
  SDL_GetWindowSize(window_, &window_w, &window_h);
  if (window_w <= 0 || window_h <= 0 || scaling_ == OutputScaling::Stretch) {
    return SDL_FRect{0.0F, 0.0F, static_cast<float>(window_w), static_cast<float>(window_h)};
  }

  const float sx = static_cast<float>(window_w) / static_cast<float>(frame_size.width);
  const float sy = static_cast<float>(window_h) / static_cast<float>(frame_size.height);
  float scale = scaling_ == OutputScaling::Fit ? std::min(sx, sy) : std::max(sx, sy);
  if (scaling_ == OutputScaling::Integer) {
    scale = std::max(1.0F, std::floor(std::min(sx, sy)));
  }
  const float w = static_cast<float>(frame_size.width) * scale;
  const float h = static_cast<float>(frame_size.height) * scale;
  return SDL_FRect{(static_cast<float>(window_w) - w) * 0.5F,
                   (static_cast<float>(window_h) - h) * 0.5F,
                   w,
                   h};
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

} // namespace cv
