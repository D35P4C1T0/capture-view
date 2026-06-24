#include "app.hpp"

#include "common.hpp"
#include "log.hpp"
#include "renderer_sdl.hpp"
#include "test_pattern.hpp"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <iostream>
#include <string>

namespace cv {

namespace {

int run_test_pattern(CliOptions& options) {
  SdlRenderer renderer("capture-view test",
                       options.size,
                       options.fullscreen,
                       options.borderless,
                       options.vsync,
                       output_scaling_from_string(options.output_scaling),
                       upscale_quality_from_string(options.upscale_quality),
                       options.render_backend == "opengl");
  renderer.set_rcas_strength(options.rcas_strength);

  TestPattern pattern(options.size);
  bool running = true;
  while (running) {
    bool restart = false;
    bool audio_restart = false;
    bool mute = false;
    float volume_delta = 0.0F;
    bool scaling_requested = false;
    running = renderer.handle_events(restart, audio_restart, mute, volume_delta, scaling_requested);
    (void)restart;
    (void)audio_restart;
    (void)mute;
    (void)volume_delta;
    (void)scaling_requested;
    renderer.render(pattern.next(), {});
    SDL_Delay(1);
  }
  return 0;
}

int unsupported(const std::string& feature) {
  std::cerr << feature << " is not supported in the Windows build yet.\n";
  std::cerr << "Current Windows support is limited to --test-pattern and SDL rendering.\n";
  return 2;
}

} // namespace

int run_app(CliOptions& options) {
  if (options.doctor) {
    std::cout << "capture-view doctor\n";
    std::cout << "platform: Windows\n";
    std::cout << "video capture: unsupported; Linux V4L2 backend is not available\n";
    std::cout << "audio monitor: unsupported; PipeWire backend is not available\n";
    std::cout << "SDL_VIDEODRIVER=" << (std::getenv("SDL_VIDEODRIVER") != nullptr ? std::getenv("SDL_VIDEODRIVER") : "(auto)")
              << '\n';
    return 0;
  }
  if (options.list_devices) {
    return unsupported("V4L2 device listing");
  }
  if (options.list_audio) {
    return unsupported("PipeWire audio listing");
  }
  if (options.gtk_ui) {
    return unsupported("GTK launcher");
  }
  if (options.wizard) {
    return unsupported("interactive setup wizard");
  }
  if (options.test_pattern) {
    return run_test_pattern(options);
  }
  log::info("Windows build launching graphical SDL preview");
  return run_test_pattern(options);
}

} // namespace cv
