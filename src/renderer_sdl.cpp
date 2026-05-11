#include "renderer_sdl.hpp"

#include <array>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <fstream>
#include <optional>
#include <sstream>
#include <utility>

namespace cv {

namespace {

double elapsed_ms(Clock::time_point start, Clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

using Glyph = std::array<const char*, 7>;

struct BitmapGlyph {
  std::array<uint8_t, 16> rows{};
  int advance = 6;
};

struct BitmapFont {
  std::array<BitmapGlyph, 95> glyphs{};
  bool loaded = false;
};

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

void draw_legacy_text(SDL_Renderer* renderer, float x, float y, const std::string& text) {
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

std::optional<std::ifstream> open_kirsch_font() {
  const std::array paths = {
    "data/kirsch.bdf",
    "/usr/local/share/capture-view/kirsch.bdf",
    "/usr/share/capture-view/kirsch.bdf",
  };
  for (const char* path : paths) {
    std::ifstream file(path);
    if (file.good()) {
      return file;
    }
  }
  return std::nullopt;
}

BitmapFont load_kirsch_font() {
  BitmapFont font;
  auto file = open_kirsch_font();
  if (!file.has_value()) {
    return font;
  }

  constexpr int ascent = 12;
  std::string line;
  int encoding = -1;
  int advance = 6;
  int width = 0;
  int height = 0;
  int x_offset = 0;
  int y_offset = 0;
  std::vector<std::string> bitmap;
  bool in_char = false;
  bool in_bitmap = false;

  auto reset = [&]() {
    encoding = -1;
    advance = 6;
    width = 0;
    height = 0;
    x_offset = 0;
    y_offset = 0;
    bitmap.clear();
    in_bitmap = false;
  };

  auto finish = [&]() {
    if (encoding < 32 || encoding > 126 || height <= 0 || width <= 0) {
      return;
    }
    BitmapGlyph glyph;
    glyph.advance = advance;
    const size_t row_count = std::min(bitmap.size(), static_cast<size_t>(height));
    for (size_t row = 0; row < row_count; ++row) {
      const std::string& hex = bitmap[row];
      if (hex.empty()) {
        continue;
      }
      const auto value = static_cast<uint32_t>(std::stoul(hex, nullptr, 16));
      const int bits = static_cast<int>(hex.size() * 4);
      const int global_y = ascent - (y_offset + height) + static_cast<int>(row);
      if (global_y < 0 || global_y >= static_cast<int>(glyph.rows.size())) {
        continue;
      }
      for (int bit = 0; bit < width && bit < 8; ++bit) {
        const uint32_t mask = 1U << static_cast<unsigned int>(bits - 1 - bit);
        if ((value & mask) == 0U) {
          continue;
        }
        const int x = x_offset + bit;
        if (x >= 0 && x < 8) {
          glyph.rows[static_cast<size_t>(global_y)] |= static_cast<uint8_t>(1U << (7 - x));
        }
      }
    }
    font.glyphs[static_cast<size_t>(encoding - 32)] = glyph;
    font.loaded = true;
  };

  while (std::getline(*file, line)) {
    if (line.starts_with("STARTCHAR")) {
      in_char = true;
      reset();
      continue;
    }
    if (line == "ENDCHAR") {
      finish();
      in_char = false;
      in_bitmap = false;
      continue;
    }
    if (!in_char) {
      continue;
    }
    if (in_bitmap) {
      bitmap.push_back(line);
      continue;
    }

    std::istringstream stream(line);
    std::string key;
    stream >> key;
    if (key == "ENCODING") {
      stream >> encoding;
    } else if (key == "DWIDTH") {
      stream >> advance;
    } else if (key == "BBX") {
      stream >> width >> height >> x_offset >> y_offset;
    } else if (key == "BITMAP") {
      in_bitmap = true;
    }
  }

  return font;
}

const BitmapFont& kirsch_font() {
  static const BitmapFont font = load_kirsch_font();
  return font;
}

void draw_text(SDL_Renderer* renderer, float x, float y, const std::string& text, float scale = 1.0F) {
  const BitmapFont& font = kirsch_font();
  if (!font.loaded) {
    draw_legacy_text(renderer, x, y, text);
    return;
  }

  SDL_SetRenderDrawColor(renderer, 245, 245, 245, 255);
  for (char c : text) {
    const unsigned char raw = static_cast<unsigned char>(c);
    const BitmapGlyph& glyph = raw >= 32 && raw <= 126 ? font.glyphs[static_cast<size_t>(raw - 32)]
                                                       : font.glyphs[static_cast<size_t>('?' - 32)];
    for (size_t row = 0; row < glyph.rows.size(); ++row) {
      for (int col = 0; col < 8; ++col) {
        if ((glyph.rows[row] & static_cast<uint8_t>(1U << (7 - col))) == 0U) {
          continue;
        }
        SDL_FRect rect{x + static_cast<float>(col) * scale,
                       y + static_cast<float>(row) * scale,
                       scale,
                       scale};
        SDL_RenderFillRect(renderer, &rect);
      }
    }
    x += static_cast<float>(glyph.advance + 1) * scale;
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
  ensure_texture(size, SDL_PIXELFORMAT_RGBA32);
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
    } else if (key == SDLK_G) {
      show_gui_ = !show_gui_;
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
  ensure_texture(frame.size, SDL_PIXELFORMAT_RGBA32);
  if (show_stats_) {
    SDL_SetWindowTitle(window_, stats_text.c_str());
  }

  const auto upload_start = Clock::now();
  if (!SDL_UpdateTexture(texture_, nullptr, frame.pixels.data(), static_cast<int>(frame.size.width * 4))) {
    throw AppError(std::string("SDL_UpdateTexture failed: ") + SDL_GetError());
  }
  const auto upload_end = Clock::now();
  stats_.upload_ms = elapsed_ms(upload_start, upload_end);

  render_texture(frame.size, stats_text);
}

void SdlRenderer::render(FrameView frame, const std::string& stats_text) {
  const SDL_PixelFormat format = frame.format == PixelFormat::Yuyv ? SDL_PIXELFORMAT_YUY2 :
                                 frame.format == PixelFormat::Nv12 ? SDL_PIXELFORMAT_NV12 :
                                                                      SDL_PIXELFORMAT_UNKNOWN;
  if (format == SDL_PIXELFORMAT_UNKNOWN) {
    throw AppError("raw renderer supports yuyv and nv12 only");
  }
  ensure_texture(frame.size, format);
  if (show_stats_) {
    SDL_SetWindowTitle(window_, stats_text.c_str());
  }

  const auto upload_start = Clock::now();
  const auto* data = reinterpret_cast<const uint8_t*>(frame.bytes.data());
  const size_t y_size = static_cast<size_t>(frame.size.width) * frame.size.height;
  if (frame.format == PixelFormat::Yuyv) {
    const size_t expected = y_size * 2;
    if (frame.bytes.size() < expected) {
      throw AppError("short YUYV frame");
    }
    if (!SDL_UpdateTexture(texture_, nullptr, data, static_cast<int>(frame.size.width * 2))) {
      throw AppError(std::string("SDL_UpdateTexture YUYV failed: ") + SDL_GetError());
    }
  } else {
    const size_t expected = y_size + y_size / 2;
    if (frame.bytes.size() < expected) {
      throw AppError("short NV12 frame");
    }
    if (!SDL_UpdateNVTexture(texture_,
                             nullptr,
                             data,
                             static_cast<int>(frame.size.width),
                             data + y_size,
                             static_cast<int>(frame.size.width))) {
      throw AppError(std::string("SDL_UpdateNVTexture failed: ") + SDL_GetError());
    }
  }
  const auto upload_end = Clock::now();
  stats_.upload_ms = elapsed_ms(upload_start, upload_end);

  render_texture(frame.size, stats_text);
}

void SdlRenderer::set_gui_lines(std::vector<std::string> lines) {
  gui_lines_ = std::move(lines);
}

void SdlRenderer::set_vsync(bool enabled) {
  vsync_ = enabled;
  SDL_SetRenderVSync(renderer_, enabled ? 1 : 0);
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
}

void SdlRenderer::render_texture(Size frame_size, const std::string& stats_text) {
  SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
  SDL_RenderClear(renderer_);
  const SDL_FRect dst = destination_rect(frame_size);
  SDL_RenderTexture(renderer_, texture_, nullptr, &dst);

  if (show_stats_) {
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 180);
    SDL_FRect bg{8.0F, 8.0F, 1060.0F, 24.0F};
    SDL_RenderFillRect(renderer_, &bg);
    draw_text(renderer_, 14.0F, 12.0F, stats_text);
  }

  if (show_gui_ && !gui_lines_.empty()) {
    const float width = 560.0F;
    const float height = 32.0F + static_cast<float>(gui_lines_.size()) * 18.0F;
    SDL_SetRenderDrawColor(renderer_, 12, 18, 28, 220);
    SDL_FRect bg{14.0F, 44.0F, width, height};
    SDL_RenderFillRect(renderer_, &bg);
    draw_text(renderer_, 24.0F, 54.0F, "gui overlay", 1.0F);
    float y = 76.0F;
    for (const auto& line : gui_lines_) {
      draw_text(renderer_, 24.0F, y, line, 1.0F);
      y += 18.0F;
    }
  }

  const auto present_start = Clock::now();
  SDL_RenderPresent(renderer_);
  const auto present_end = Clock::now();
  stats_.present_ms = elapsed_ms(present_start, present_end);
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
