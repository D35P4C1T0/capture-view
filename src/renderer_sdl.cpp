#define GL_GLEXT_PROTOTYPES
#include "renderer_sdl.hpp"

#include <SDL3/SDL_opengl.h>

#include <array>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <utility>
#ifndef _WIN32
#include <unistd.h>
#endif

#include "log.hpp"

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

std::filesystem::path executable_dir() {
#ifdef _WIN32
  const char* base_path = SDL_GetBasePath();
  if (base_path == nullptr) {
    return {};
  }
  std::filesystem::path path(base_path);
  return path;
#else
  std::array<char, 4096> path{};
  const ssize_t length = ::readlink("/proc/self/exe", path.data(), path.size() - 1);
  if (length <= 0) {
    return {};
  }
  path[static_cast<size_t>(length)] = '\0';
  return std::filesystem::path(path.data()).parent_path();
#endif
}

std::optional<std::ifstream> open_kirsch_font() {
  const std::filesystem::path exe_dir = executable_dir();
  std::vector<std::filesystem::path> paths;
  if (!exe_dir.empty()) {
    paths.push_back(exe_dir.parent_path() / "share/capture-view/kirsch.bdf");
    paths.push_back(exe_dir.parent_path() / "data/kirsch.bdf");
  }
  paths.emplace_back("data/kirsch.bdf");
  paths.emplace_back("usr/share/capture-view/kirsch.bdf");
  paths.emplace_back("/usr/local/share/capture-view/kirsch.bdf");
  paths.emplace_back("/usr/share/capture-view/kirsch.bdf");

  for (const auto& path : paths) {
    std::ifstream file(path);
    if (file.good()) {
      log::info("overlay font loaded: ", path.string());
      return file;
    }
  }
  log::warning("overlay font not found; using fallback text renderer");
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

const std::vector<std::string>& help_lines() {
  static const std::vector<std::string> lines{
      "keyboard",
      "F fullscreen",
      "Esc exit fullscreen / quit",
      "Q quit",
      "S toggle stats overlay",
      "G toggle status panel",
      "? toggle this help",
      "V toggle vsync",
      "O cycle scaling: fit fill stretch integer",
      "U cycle upscale: nearest bilinear bilinear-rcas",
      "[ ] adjust RCAS strength",
      "R restart capture",
      "A restart audio",
      "M mute audio",
      "+ - volume",
      "Alt+B toggle borderless",
  };
  return lines;
}

#ifndef _WIN32
const char* gl_error_log(GLuint object, bool program) {
  static std::array<char, 2048> log{};
  GLsizei length = 0;
  if (program) {
    glGetProgramInfoLog(object, static_cast<GLsizei>(log.size()), &length, log.data());
  } else {
    glGetShaderInfoLog(object, static_cast<GLsizei>(log.size()), &length, log.data());
  }
  log[std::min<size_t>(static_cast<size_t>(std::max<GLsizei>(length, 0)), log.size() - 1)] = '\0';
  return log.data();
}

GLuint compile_shader(GLenum type, const char* source) {
  const GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint ok = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (ok != GL_TRUE) {
    const std::string message = gl_error_log(shader, false);
    glDeleteShader(shader);
    throw AppError("OpenGL shader compile failed: " + message);
  }
  return shader;
}

GLuint make_program(const char* fragment_source) {
  static constexpr const char* vertex_source = R"glsl(
    #version 120
    attribute vec2 aPosition;
    attribute vec2 aTexCoord;
    varying vec2 vUV;
    void main() {
      vUV = aTexCoord;
      gl_Position = vec4(aPosition, 0.0, 1.0);
    }
  )glsl";
  const GLuint vertex = compile_shader(GL_VERTEX_SHADER, vertex_source);
  const GLuint fragment = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
  const GLuint program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  glBindAttribLocation(program, 0, "aPosition");
  glBindAttribLocation(program, 1, "aTexCoord");
  glLinkProgram(program);
  glDeleteShader(vertex);
  glDeleteShader(fragment);
  GLint ok = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (ok != GL_TRUE) {
    const std::string message = gl_error_log(program, true);
    glDeleteProgram(program);
    throw AppError("OpenGL shader link failed: " + message);
  }
  return program;
}

void upload_quad(GLuint vbo, SDL_FRect rect, int width, int height, bool flip_v = false) {
  const float left = rect.x / static_cast<float>(width) * 2.0F - 1.0F;
  const float right = (rect.x + rect.w) / static_cast<float>(width) * 2.0F - 1.0F;
  const float top = 1.0F - rect.y / static_cast<float>(height) * 2.0F;
  const float bottom = 1.0F - (rect.y + rect.h) / static_cast<float>(height) * 2.0F;
  const float top_uv = flip_v ? 1.0F : 0.0F;
  const float bottom_uv = flip_v ? 0.0F : 1.0F;
  const std::array<float, 24> vertices{
      left, bottom, 0.0F, bottom_uv,
      right, bottom, 1.0F, bottom_uv,
      right, top, 1.0F, top_uv,
      left, bottom, 0.0F, bottom_uv,
      right, top, 1.0F, top_uv,
      left, top, 0.0F, top_uv,
  };
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
               vertices.data(), GL_STREAM_DRAW);
}

void draw_bound_quad() {
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                        reinterpret_cast<const void*>(2 * sizeof(float)));
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glDisableVertexAttribArray(0);
  glDisableVertexAttribArray(1);
}

#endif

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

SdlRenderer::SdlRenderer(std::string title, Size size, bool fullscreen, bool borderless, bool vsync, OutputScaling scaling,
                         UpscaleQuality upscale_quality, bool force_opengl)
    : fullscreen_(fullscreen), borderless_(borderless), vsync_(vsync), scaling_(scaling),
      upscale_quality_(upscale_quality) {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    throw AppError(std::string("SDL_Init failed: ") + SDL_GetError());
  }

  Uint32 flags = SDL_WINDOW_RESIZABLE;
  flags |= SDL_WINDOW_OPENGL;
  if (fullscreen_) {
    flags |= SDL_WINDOW_FULLSCREEN;
  }
  if (borderless_) {
    flags |= SDL_WINDOW_BORDERLESS;
  }

  window_ = SDL_CreateWindow(title.c_str(), static_cast<int>(size.width), static_cast<int>(size.height), flags);
  if (window_ == nullptr) {
    throw AppError(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
  }

  if (force_opengl || upscale_quality_ == UpscaleQuality::BilinearRcas) {
    (void)enable_gl_rcas();
    if (force_opengl && !gl_ready_) {
      throw AppError("OpenGL render backend requested but unavailable");
    }
  }

  if (!gl_ready_) {
    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (renderer_ == nullptr) {
      throw AppError(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
    }
    set_vsync(vsync_);
    ensure_texture(size, SDL_PIXELFORMAT_RGBA32);
  }
}

SdlRenderer::~SdlRenderer() {
  if (gl_vbo_ != 0) {
    glDeleteBuffers(1, &gl_vbo_);
  }
  if (gl_framebuffer_ != 0) {
    glDeleteFramebuffers(1, &gl_framebuffer_);
  }
  if (gl_target_texture_ != 0) {
    glDeleteTextures(1, &gl_target_texture_);
  }
  if (gl_source_texture_ != 0) {
    glDeleteTextures(1, &gl_source_texture_);
  }
  if (gl_yuyv_texture_ != 0) {
    glDeleteTextures(1, &gl_yuyv_texture_);
  }
  if (gl_nv12_y_texture_ != 0) {
    glDeleteTextures(1, &gl_nv12_y_texture_);
  }
  if (gl_nv12_uv_texture_ != 0) {
    glDeleteTextures(1, &gl_nv12_uv_texture_);
  }
  if (gl_video_program_ != 0) {
    glDeleteProgram(gl_video_program_);
  }
  if (gl_yuyv_program_ != 0) {
    glDeleteProgram(gl_yuyv_program_);
  }
  if (gl_nv12_program_ != 0) {
    glDeleteProgram(gl_nv12_program_);
  }
  if (gl_rcas_program_ != 0) {
    glDeleteProgram(gl_rcas_program_);
  }
  if (gl_color_program_ != 0) {
    glDeleteProgram(gl_color_program_);
  }
  if (gl_context_ != nullptr) {
    SDL_GL_DestroyContext(gl_context_);
  }
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

#ifndef _WIN32
bool SdlRenderer::enable_gl_rcas() {
  if (gl_ready_) {
    return true;
  }
  try {
    if (texture_ != nullptr) {
      SDL_DestroyTexture(texture_);
      texture_ = nullptr;
    }
    if (renderer_ != nullptr) {
      SDL_DestroyRenderer(renderer_);
      renderer_ = nullptr;
    }
    gl_context_ = SDL_GL_CreateContext(window_);
    if (gl_context_ == nullptr) {
      throw AppError(std::string("SDL_GL_CreateContext failed: ") + SDL_GetError());
    }
    (void)SDL_GL_SetSwapInterval(vsync_ ? 1 : 0);
    gl_video_program_ = make_program(R"glsl(
      #version 120
      uniform sampler2D uTexture;
      varying vec2 vUV;
      void main() {
        gl_FragColor = texture2D(uTexture, vUV);
      }
    )glsl");
    gl_yuyv_program_ = make_program(R"glsl(
      #version 120
      uniform sampler2D uTexture;
      uniform vec2 uFrameSize;
      varying vec2 vUV;
      vec3 yuv_to_rgb(float y, float u, float v) {
        float c = y - 0.0625;
        float d = u - 0.5;
        float e = v - 0.5;
        return clamp(vec3(1.1643 * c + 1.5958 * e,
                          1.1643 * c - 0.3917 * d - 0.8129 * e,
                          1.1643 * c + 2.0170 * d), 0.0, 1.0);
      }
      void main() {
        float pixelX = clamp(floor(vUV.x * uFrameSize.x), 0.0, uFrameSize.x - 1.0);
        float pairX = floor(pixelX * 0.5);
        vec2 packedUV = vec2((pairX + 0.5) / (uFrameSize.x * 0.5), vUV.y);
        vec4 yuyv = texture2D(uTexture, packedUV);
        float y = mod(pixelX, 2.0) < 1.0 ? yuyv.r : yuyv.b;
        gl_FragColor = vec4(yuv_to_rgb(y, yuyv.g, yuyv.a), 1.0);
      }
    )glsl");
    gl_nv12_program_ = make_program(R"glsl(
      #version 120
      uniform sampler2D uY;
      uniform sampler2D uUV;
      varying vec2 vUV;
      vec3 yuv_to_rgb(float y, float u, float v) {
        float c = y - 0.0625;
        float d = u - 0.5;
        float e = v - 0.5;
        return clamp(vec3(1.1643 * c + 1.5958 * e,
                          1.1643 * c - 0.3917 * d - 0.8129 * e,
                          1.1643 * c + 2.0170 * d), 0.0, 1.0);
      }
      void main() {
        float y = texture2D(uY, vUV).r;
        vec2 uv = texture2D(uUV, vUV).ra;
        gl_FragColor = vec4(yuv_to_rgb(y, uv.x, uv.y), 1.0);
      }
    )glsl");
    gl_rcas_program_ = make_program(R"glsl(
      #version 120
      uniform sampler2D uTexture;
      uniform vec2 uTexelSize;
      uniform float uStrength;
      varying vec2 vUV;
      void main() {
        vec4 centerSample = texture2D(uTexture, vUV);
        vec3 c = centerSample.rgb;
        vec3 l = texture2D(uTexture, vUV + vec2(-uTexelSize.x, 0.0)).rgb;
        vec3 r = texture2D(uTexture, vUV + vec2( uTexelSize.x, 0.0)).rgb;
        vec3 t = texture2D(uTexture, vUV + vec2(0.0, -uTexelSize.y)).rgb;
        vec3 b = texture2D(uTexture, vUV + vec2(0.0,  uTexelSize.y)).rgb;
        vec3 minRGB = min(c, min(min(l, r), min(t, b)));
        vec3 maxRGB = max(c, max(max(l, r), max(t, b)));
        vec3 blur = (l + r + t + b) * 0.25;
        vec3 detail = c - blur;
        vec3 sharpened = clamp(c + detail * uStrength, minRGB, maxRGB);
        gl_FragColor = vec4(sharpened, centerSample.a);
      }
    )glsl");
    gl_color_program_ = make_program(R"glsl(
      #version 120
      uniform vec4 uColor;
      varying vec2 vUV;
      void main() {
        gl_FragColor = uColor;
      }
    )glsl");
    glGenBuffers(1, &gl_vbo_);
    gl_ready_ = true;
    return true;
  } catch (const AppError& error) {
    log::warning("bilinear-rcas unavailable; falling back to bilinear: ", error.what());
    upscale_quality_ = UpscaleQuality::Bilinear;
    gl_warned_ = true;
    if (gl_context_ != nullptr) {
      SDL_GL_DestroyContext(gl_context_);
      gl_context_ = nullptr;
    }
    if (renderer_ == nullptr) {
      renderer_ = SDL_CreateRenderer(window_, nullptr);
      if (renderer_ != nullptr) {
        set_vsync(vsync_);
      }
    }
    return false;
  }
}

#else
bool SdlRenderer::enable_gl_rcas() { return false; }
#endif

bool SdlRenderer::handle_events(bool& restart_requested, bool& audio_restart_requested, bool& mute_requested,
                                float& volume_delta, bool& scaling_requested) {
  SDL_Event event{};
  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_EVENT_QUIT) {
      return false;
    }
    if (event.type == SDL_EVENT_MOUSE_MOTION) {
      last_mouse_motion_ = Clock::now();
      show_cursor();
      continue;
    }
    if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
      window_focused_ = true;
      last_mouse_motion_ = Clock::now();
      show_cursor();
      continue;
    }
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
      window_focused_ = false;
      show_cursor();
      continue;
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
    } else if (key == SDLK_B && (event.key.mod & SDL_KMOD_ALT) != 0) {
      toggle_borderless();
    } else if (key == SDLK_V) {
      set_vsync(!vsync_);
    } else if (key == SDLK_S) {
      show_stats_ = !show_stats_;
    } else if (key == SDLK_G) {
      show_gui_ = !show_gui_;
    } else if (key == SDLK_QUESTION || (key == SDLK_SLASH && (event.key.mod & SDL_KMOD_SHIFT) != 0)) {
      show_help_ = !show_help_;
    } else if (key == SDLK_R) {
      restart_requested = true;
    } else if (key == SDLK_A) {
      audio_restart_requested = true;
    } else if (key == SDLK_M) {
      mute_requested = true;
    } else if (key == SDLK_O) {
      cycle_scaling();
      scaling_requested = true;
    } else if (key == SDLK_U) {
      cycle_upscale_quality();
    } else if (key == SDLK_LEFTBRACKET) {
      set_rcas_strength(rcas_strength_ - 0.05F);
      log::info("rcas strength=", rcas_strength_);
    } else if (key == SDLK_RIGHTBRACKET) {
      set_rcas_strength(rcas_strength_ + 0.05F);
      log::info("rcas strength=", rcas_strength_);
    } else if (key == SDLK_EQUALS || key == SDLK_PLUS) {
      volume_delta = 0.05F;
    } else if (key == SDLK_MINUS) {
      volume_delta = -0.05F;
    }
  }
  update_cursor_visibility();
  return true;
}

void SdlRenderer::render(const RgbaFrame& frame, const std::string& stats_text) {
  if (gl_ready_) {
    ensure_gl_texture(frame.size);
    update_stats_title(stats_text);
    const auto upload_start = Clock::now();
    glBindTexture(GL_TEXTURE_2D, gl_source_texture_);
    const GLint filter = upscale_quality_ == UpscaleQuality::Nearest ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(frame.size.width),
                    static_cast<GLsizei>(frame.size.height), GL_RGBA, GL_UNSIGNED_BYTE,
                    frame.pixels.data());
    const auto upload_end = Clock::now();
    stats_.upload_ms = elapsed_ms(upload_start, upload_end);
    render_gl_texture(frame.size, stats_text);
    return;
  }

  ensure_texture(frame.size, SDL_PIXELFORMAT_RGBA32);
  update_stats_title(stats_text);

  const auto upload_start = Clock::now();
  if (!SDL_UpdateTexture(texture_, nullptr, frame.pixels.data(), static_cast<int>(frame.size.width * 4))) {
    throw AppError(std::string("SDL_UpdateTexture failed: ") + SDL_GetError());
  }
  const auto upload_end = Clock::now();
  stats_.upload_ms = elapsed_ms(upload_start, upload_end);

  render_texture(frame.size, stats_text);
}

void SdlRenderer::render(FrameView frame, const std::string& stats_text) {
#ifndef _WIN32
  if (gl_ready_) {
    update_stats_title(stats_text);
    int window_w = 0;
    int window_h = 0;
    SDL_GetWindowSizeInPixels(window_, &window_w, &window_h);
    if (window_w <= 0 || window_h <= 0) {
      return;
    }
    const auto upload_start = Clock::now();
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const auto* data = reinterpret_cast<const uint8_t*>(frame.bytes.data());
    const size_t y_size = static_cast<size_t>(frame.size.width) * frame.size.height;
    GLuint program = 0;
    if (frame.format == PixelFormat::Yuyv) {
      const size_t expected = y_size * 2;
      if (frame.bytes.size() < expected) {
        throw AppError("short YUYV frame");
      }
      if (gl_yuyv_texture_ == 0) {
        glGenTextures(1, &gl_yuyv_texture_);
      }
      glBindTexture(GL_TEXTURE_2D, gl_yuyv_texture_);
      const GLint filter = upscale_quality_ == UpscaleQuality::Nearest ? GL_NEAREST : GL_LINEAR;
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      if (gl_yuyv_size_.width != frame.size.width || gl_yuyv_size_.height != frame.size.height) {
        gl_yuyv_size_ = frame.size;
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_RGBA,
                     static_cast<GLsizei>(frame.size.width / 2),
                     static_cast<GLsizei>(frame.size.height),
                     0,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     nullptr);
      }
      glTexSubImage2D(GL_TEXTURE_2D,
                      0,
                      0,
                      0,
                      static_cast<GLsizei>(frame.size.width / 2),
                      static_cast<GLsizei>(frame.size.height),
                      GL_RGBA,
                      GL_UNSIGNED_BYTE,
                      data);
      program = gl_yuyv_program_;
    } else if (frame.format == PixelFormat::Nv12) {
      const size_t expected = y_size + y_size / 2;
      if (frame.bytes.size() < expected) {
        throw AppError("short NV12 frame");
      }
      if (gl_nv12_y_texture_ == 0) {
        glGenTextures(1, &gl_nv12_y_texture_);
      }
      if (gl_nv12_uv_texture_ == 0) {
        glGenTextures(1, &gl_nv12_uv_texture_);
      }
      const GLint filter = upscale_quality_ == UpscaleQuality::Nearest ? GL_NEAREST : GL_LINEAR;
      const bool resize_nv12 = gl_nv12_size_.width != frame.size.width || gl_nv12_size_.height != frame.size.height;
      glBindTexture(GL_TEXTURE_2D, gl_nv12_y_texture_);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      if (resize_nv12) {
        gl_nv12_size_ = frame.size;
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_LUMINANCE,
                     static_cast<GLsizei>(frame.size.width),
                     static_cast<GLsizei>(frame.size.height),
                     0,
                     GL_LUMINANCE,
                     GL_UNSIGNED_BYTE,
                     nullptr);
      }
      glTexSubImage2D(GL_TEXTURE_2D,
                      0,
                      0,
                      0,
                      static_cast<GLsizei>(frame.size.width),
                      static_cast<GLsizei>(frame.size.height),
                      GL_LUMINANCE,
                      GL_UNSIGNED_BYTE,
                      data);
      glBindTexture(GL_TEXTURE_2D, gl_nv12_uv_texture_);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      if (resize_nv12) {
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_LUMINANCE_ALPHA,
                     static_cast<GLsizei>(frame.size.width / 2),
                     static_cast<GLsizei>(frame.size.height / 2),
                     0,
                     GL_LUMINANCE_ALPHA,
                     GL_UNSIGNED_BYTE,
                     nullptr);
      }
      glTexSubImage2D(GL_TEXTURE_2D,
                      0,
                      0,
                      0,
                      static_cast<GLsizei>(frame.size.width / 2),
                      static_cast<GLsizei>(frame.size.height / 2),
                      GL_LUMINANCE_ALPHA,
                      GL_UNSIGNED_BYTE,
                      data + y_size);
      program = gl_nv12_program_;
    } else {
      throw AppError("OpenGL raw renderer supports yuyv and nv12 only");
    }
    const auto upload_end = Clock::now();
    stats_.upload_ms = elapsed_ms(upload_start, upload_end);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, window_w, window_h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(program);
    if (frame.format == PixelFormat::Yuyv) {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, gl_yuyv_texture_);
      glUniform1i(glGetUniformLocation(program, "uTexture"), 0);
      glUniform2f(glGetUniformLocation(program, "uFrameSize"),
                  static_cast<float>(frame.size.width),
                  static_cast<float>(frame.size.height));
    } else {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, gl_nv12_y_texture_);
      glUniform1i(glGetUniformLocation(program, "uY"), 0);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, gl_nv12_uv_texture_);
      glUniform1i(glGetUniformLocation(program, "uUV"), 1);
      glActiveTexture(GL_TEXTURE0);
    }
    upload_quad(gl_vbo_, destination_rect(frame.size), window_w, window_h);
    draw_bound_quad();
    draw_gl_overlays(stats_text, window_w, window_h);
    const auto present_start = Clock::now();
    SDL_GL_SwapWindow(window_);
    const auto present_end = Clock::now();
    stats_.present_ms = elapsed_ms(present_start, present_end);
    return;
  }
#endif

  const SDL_PixelFormat format = frame.format == PixelFormat::Yuyv ? SDL_PIXELFORMAT_YUY2 :
                                 frame.format == PixelFormat::Nv12 ? SDL_PIXELFORMAT_NV12 :
                                                                      SDL_PIXELFORMAT_UNKNOWN;
  if (format == SDL_PIXELFORMAT_UNKNOWN) {
    throw AppError("raw renderer supports yuyv and nv12 only");
  }
  ensure_texture(frame.size, format);
  update_stats_title(stats_text);

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
  if (gl_ready_) {
    (void)SDL_GL_SetSwapInterval(enabled ? 1 : 0);
  } else {
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
  if (!SDL_SetTextureScaleMode(texture_, scale_mode)) {
    throw AppError(std::string("SDL_SetTextureScaleMode failed: ") + SDL_GetError());
  }
}

#ifndef _WIN32
void SdlRenderer::ensure_gl_texture(Size size) {
  if (gl_source_texture_ != 0 && texture_size_.width == size.width && texture_size_.height == size.height) {
    return;
  }
  if (gl_source_texture_ == 0) {
    glGenTextures(1, &gl_source_texture_);
  }
  texture_size_ = size;
  glBindTexture(GL_TEXTURE_2D, gl_source_texture_);
  const GLint filter = upscale_quality_ == UpscaleQuality::Nearest ? GL_NEAREST : GL_LINEAR;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(size.width),
               static_cast<GLsizei>(size.height), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
}

void SdlRenderer::ensure_gl_target(Size size) {
  if (gl_target_texture_ != 0 && gl_target_size_.width == size.width && gl_target_size_.height == size.height) {
    return;
  }
  gl_target_size_ = size;
  if (gl_target_texture_ == 0) {
    glGenTextures(1, &gl_target_texture_);
  }
  glBindTexture(GL_TEXTURE_2D, gl_target_texture_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(size.width),
               static_cast<GLsizei>(size.height), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  if (gl_framebuffer_ == 0) {
    glGenFramebuffers(1, &gl_framebuffer_);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, gl_framebuffer_);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl_target_texture_, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    throw AppError("OpenGL RCAS framebuffer incomplete");
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SdlRenderer::render_texture(Size frame_size, const std::string& stats_text) {
  SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
  SDL_RenderClear(renderer_);
  const SDL_FRect dst = destination_rect(frame_size);
  SDL_RenderTexture(renderer_, texture_, nullptr, &dst);

  if (show_stats_) {
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 180);
    SDL_FRect bg{8.0F, 8.0F, 760.0F, 24.0F};
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

  if (show_help_) {
    int output_w = 0;
    int output_h = 0;
    SDL_GetCurrentRenderOutputSize(renderer_, &output_w, &output_h);
    const auto& lines = help_lines();
    const float width = 430.0F;
    const float height = 32.0F + static_cast<float>(lines.size()) * 18.0F;
    const float x = std::max(14.0F, static_cast<float>(output_w) - width - 14.0F);
    const float panel_y = std::max(44.0F, static_cast<float>(output_h) - height - 14.0F);
    SDL_SetRenderDrawColor(renderer_, 8, 10, 14, 235);
    SDL_FRect bg{x, panel_y, width, height};
    SDL_RenderFillRect(renderer_, &bg);
    float y = panel_y + 10.0F;
    for (const auto& line : lines) {
      draw_text(renderer_, x + 10.0F, y, line, 1.0F);
      y += 18.0F;
    }
  }

  const auto present_start = Clock::now();
  SDL_RenderPresent(renderer_);
  const auto present_end = Clock::now();
  stats_.present_ms = elapsed_ms(present_start, present_end);
}

void SdlRenderer::render_gl_texture(Size frame_size, const std::string& stats_text) {
  int window_w = 0;
  int window_h = 0;
  SDL_GetWindowSizeInPixels(window_, &window_w, &window_h);
  if (window_w <= 0 || window_h <= 0) {
    return;
  }
  ensure_gl_target(Size{static_cast<uint32_t>(window_w), static_cast<uint32_t>(window_h)});
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_BLEND);

  if (upscale_quality_ != UpscaleQuality::BilinearRcas || rcas_strength_ <= 0.0F) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, window_w, window_h);
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(gl_video_program_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gl_source_texture_);
    glUniform1i(glGetUniformLocation(gl_video_program_, "uTexture"), 0);
    upload_quad(gl_vbo_, destination_rect(frame_size), window_w, window_h);
    draw_bound_quad();
    draw_gl_overlays(stats_text, window_w, window_h);
    const auto present_start = Clock::now();
    SDL_GL_SwapWindow(window_);
    const auto present_end = Clock::now();
    stats_.present_ms = elapsed_ms(present_start, present_end);
    return;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, gl_framebuffer_);
  glViewport(0, 0, window_w, window_h);
  glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
  glClear(GL_COLOR_BUFFER_BIT);
  glUseProgram(gl_video_program_);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, gl_source_texture_);
  glUniform1i(glGetUniformLocation(gl_video_program_, "uTexture"), 0);
  upload_quad(gl_vbo_, destination_rect(frame_size), window_w, window_h);
  draw_bound_quad();

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, window_w, window_h);
  glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
  glClear(GL_COLOR_BUFFER_BIT);
  glUseProgram(gl_rcas_program_);
  glBindTexture(GL_TEXTURE_2D, gl_target_texture_);
  glUniform1i(glGetUniformLocation(gl_rcas_program_, "uTexture"), 0);
  glUniform2f(glGetUniformLocation(gl_rcas_program_, "uTexelSize"),
              1.0F / static_cast<float>(window_w), 1.0F / static_cast<float>(window_h));
  glUniform1f(glGetUniformLocation(gl_rcas_program_, "uStrength"), rcas_strength_);
  upload_quad(gl_vbo_, SDL_FRect{0.0F, 0.0F, static_cast<float>(window_w), static_cast<float>(window_h)},
              window_w, window_h, true);
  draw_bound_quad();

  draw_gl_overlays(stats_text, window_w, window_h);

  const auto present_start = Clock::now();
  SDL_GL_SwapWindow(window_);
  const auto present_end = Clock::now();
  stats_.present_ms = elapsed_ms(present_start, present_end);
}

void SdlRenderer::draw_gl_overlays(const std::string& stats_text, int window_w, int window_h) {
  if ((!show_stats_ && (!show_gui_ || gui_lines_.empty()) && !show_help_) || gl_color_program_ == 0) {
    return;
  }

  const auto draw_text_gl = [&](float x, float y, const std::string& text) {
    const BitmapFont& font = kirsch_font();
    for (char ch : text) {
      const unsigned char raw = static_cast<unsigned char>(ch);
      const BitmapGlyph& glyph = font.loaded && raw >= 32 && raw <= 126
                                     ? font.glyphs[static_cast<size_t>(raw - 32)]
                                     : BitmapGlyph{};
      for (size_t row = 0; row < glyph.rows.size(); ++row) {
        for (int col = 0; col < 8; ++col) {
          if ((glyph.rows[row] & static_cast<uint8_t>(1U << (7 - col))) == 0U) {
            continue;
          }
          upload_quad(gl_vbo_, SDL_FRect{x + static_cast<float>(col),
                                          y + static_cast<float>(row),
                                          1.0F,
                                          1.0F},
                      window_w, window_h);
          draw_bound_quad();
        }
      }
      x += static_cast<float>((font.loaded ? glyph.advance : 6) + 1);
    }
  };

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(gl_color_program_);

  if (show_stats_) {
    glUniform4f(glGetUniformLocation(gl_color_program_, "uColor"), 0.0F, 0.0F, 0.0F, 0.70F);
    upload_quad(gl_vbo_, SDL_FRect{8.0F, 8.0F, 760.0F, 24.0F}, window_w, window_h);
    draw_bound_quad();
    glUniform4f(glGetUniformLocation(gl_color_program_, "uColor"), 0.96F, 0.96F, 0.96F, 1.0F);
    draw_text_gl(14.0F, 12.0F, stats_text);
  }

  if (show_gui_ && !gui_lines_.empty()) {
    const float width = 560.0F;
    const float height = 32.0F + static_cast<float>(gui_lines_.size()) * 18.0F;
    glUniform4f(glGetUniformLocation(gl_color_program_, "uColor"), 0.05F, 0.07F, 0.11F, 0.86F);
    upload_quad(gl_vbo_, SDL_FRect{14.0F, 44.0F, width, height}, window_w, window_h);
    draw_bound_quad();
    glUniform4f(glGetUniformLocation(gl_color_program_, "uColor"), 0.96F, 0.96F, 0.96F, 1.0F);
    draw_text_gl(24.0F, 54.0F, "gui overlay");
    float y = 76.0F;
    for (const auto& line : gui_lines_) {
      draw_text_gl(24.0F, y, line);
      y += 18.0F;
    }
  }

  if (show_help_) {
    const auto& lines = help_lines();
    const float width = 430.0F;
    const float height = 32.0F + static_cast<float>(lines.size()) * 18.0F;
    const float x = std::max(14.0F, static_cast<float>(window_w) - width - 14.0F);
    const float panel_y = std::max(44.0F, static_cast<float>(window_h) - height - 14.0F);
    glUniform4f(glGetUniformLocation(gl_color_program_, "uColor"), 0.03F, 0.04F, 0.06F, 0.92F);
    upload_quad(gl_vbo_, SDL_FRect{x, panel_y, width, height}, window_w, window_h);
    draw_bound_quad();
    glUniform4f(glGetUniformLocation(gl_color_program_, "uColor"), 0.96F, 0.96F, 0.96F, 1.0F);
    float y = panel_y + 10.0F;
    for (const auto& line : lines) {
      draw_text_gl(x + 10.0F, y, line);
      y += 18.0F;
    }
  }

  glDisable(GL_BLEND);
}

void SdlRenderer::toggle_fullscreen() {
  fullscreen_ = !fullscreen_;
  SDL_SetWindowFullscreen(window_, fullscreen_);
}

void SdlRenderer::toggle_borderless() {
  borderless_ = !borderless_;
  SDL_SetWindowBordered(window_, !borderless_);
}

#else
void SdlRenderer::ensure_gl_texture(Size) {}
void SdlRenderer::ensure_gl_target(Size) {}
void SdlRenderer::render_gl_texture(Size, const std::string&) {}
void SdlRenderer::draw_gl_overlays(const std::string&, int, int) {}
#endif

void SdlRenderer::cycle_upscale_quality() {
  if (upscale_quality_ == UpscaleQuality::Nearest) {
    upscale_quality_ = UpscaleQuality::Bilinear;
  } else if (upscale_quality_ == UpscaleQuality::Bilinear) {
    if (gl_ready_ || enable_gl_rcas()) {
      upscale_quality_ = UpscaleQuality::BilinearRcas;
    } else {
      upscale_quality_ = UpscaleQuality::Nearest;
    }
  } else {
    upscale_quality_ = UpscaleQuality::Nearest;
  }
  log::info("upscale quality=", to_string(upscale_quality_));
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

void SdlRenderer::show_cursor() {
  if (cursor_visible_) {
    return;
  }
  (void)SDL_ShowCursor();
  cursor_visible_ = true;
}

void SdlRenderer::update_cursor_visibility() {
  if (!window_focused_ || !cursor_visible_) {
    return;
  }
  constexpr auto hide_after = std::chrono::seconds(3);
  if (Clock::now() - last_mouse_motion_ < hide_after) {
    return;
  }
  (void)SDL_HideCursor();
  cursor_visible_ = false;
}

void SdlRenderer::update_stats_title(const std::string& stats_text) {
  if (!show_stats_) {
    return;
  }
  const auto now = Clock::now();
  if (last_title_update_.time_since_epoch().count() != 0 &&
      now - last_title_update_ < std::chrono::milliseconds(250)) {
    return;
  }
  SDL_SetWindowTitle(window_, stats_text.c_str());
  last_title_update_ = now;
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

UpscaleQuality upscale_quality_from_string(const std::string& value) {
  if (value == "bilinear-rcas" || value == "rcas") {
    return UpscaleQuality::BilinearRcas;
  }
  if (value == "nearest") {
    return UpscaleQuality::Nearest;
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
