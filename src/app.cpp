#include "app.hpp"

#include "audio_monitor.hpp"
#include "audio_devices.hpp"
#include "audio_selector.hpp"
#include "config.hpp"
#include "gtk_ui.hpp"
#include "log.hpp"
#include "mjpeg_decoder.hpp"
#include "renderer_sdl.hpp"
#include "test_pattern.hpp"
#include "v4l2_capture.hpp"
#include "v4l2_discovery.hpp"
#include "v4l2_output.hpp"
#include "wizard.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

namespace cv {

namespace {

struct LoopStats {
  uint64_t rendered = 0;
  uint64_t decode_errors = 0;
  double decode_ms = 0.0;
  Clock::time_point last_report = Clock::now();
  Clock::time_point last_log = Clock::now();
  Clock::time_point last_render = {};
  uint64_t rendered_at_report = 0;
  double fps = 0.0;
  double frame_interval_ms = 0.0;
  double frame_jitter_ms = 0.0;
  double frame_jitter_stddev_ms = 0.0;
  double render_latency_ms = 0.0;
  double frame_age_ms = 0.0;
};

double elapsed_ms(Clock::time_point start, Clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

std::string make_title(const CliOptions& options) {
  std::ostringstream out;
  out << "capture-view " << options.size.width << "x" << options.size.height << " "
      << options.fps << "fps " << to_string(options.format);
  return out.str();
}

std::vector<CaptureConfig> capture_attempts(const CliOptions& options) {
  std::vector<CaptureConfig> configs;
  std::set<std::tuple<std::string, uint32_t, uint32_t, uint32_t, PixelFormat>> seen;
  const auto add = [&](std::string device, Size size, uint32_t fps, PixelFormat format) {
    if (format == PixelFormat::Unknown || format == PixelFormat::Auto) {
      return;
    }
    const auto key = std::tuple{device, size.width, size.height, fps, format};
    if (!seen.insert(key).second) {
      return;
    }
    configs.push_back({std::move(device), size, fps, format, options.buffer_count});
  };

  if (options.format == PixelFormat::Auto) {
    add(options.video_device, options.size, options.fps, PixelFormat::Yuyv);
    add(options.video_device, options.size, options.fps, PixelFormat::Nv12);
    add(options.video_device, options.size, options.fps, PixelFormat::Mjpeg);
  } else {
    add(options.video_device, options.size, options.fps, options.format);
  }

  const auto add_known_modes = [&](const std::string& device) {
    for (const auto& info : list_video_devices()) {
      if (info.path != device) {
        continue;
      }

      struct Candidate {
        Size size;
        uint32_t fps = 0;
        PixelFormat format = PixelFormat::Unknown;
        int score = 0;
      };
      std::vector<Candidate> candidates;

      for (const auto& format : info.formats) {
        const PixelFormat pixel_format = pixel_format_from_v4l2(format.fourcc);
        if (pixel_format == PixelFormat::Unknown) {
          continue;
        }
        if (options.format != PixelFormat::Auto && pixel_format != options.format) {
          continue;
        }
        for (const auto& size : format.sizes) {
          if (size.intervals.empty()) {
            candidates.push_back({size.size, options.fps, pixel_format, 0});
            continue;
          }
          for (const auto& interval : size.intervals) {
            if (interval.numerator == 0) {
              continue;
            }
            candidates.push_back({size.size, interval.denominator / interval.numerator, pixel_format, 0});
          }
        }
      }

      for (auto& candidate : candidates) {
        const bool exact_size = candidate.size.width == options.size.width &&
                                candidate.size.height == options.size.height;
        const bool exact_fps = candidate.fps == options.fps;
        const int format_score = candidate.format == PixelFormat::Yuyv ? 3 :
                                 candidate.format == PixelFormat::Nv12 ? 2 :
                                 candidate.format == PixelFormat::Mjpeg ? 1 : 0;
        candidate.score = (exact_size ? 100000 : 0) + (exact_fps ? 10000 : 0) +
                          static_cast<int>(candidate.size.width * candidate.size.height / 1000) +
                          static_cast<int>(candidate.fps) * 10 + format_score;
      }
      std::ranges::sort(candidates, [](const Candidate& a, const Candidate& b) {
        return a.score > b.score;
      });
      for (const auto& candidate : candidates) {
        add(device, candidate.size, candidate.fps, candidate.format);
      }
    }
  };

  add_known_modes(options.video_device);

  add(options.video_device, {1920, 1080}, 60, PixelFormat::Mjpeg);
  add(options.video_device, {1920, 1080}, 30, PixelFormat::Mjpeg);
  add(options.video_device, {1280, 720}, 60, PixelFormat::Yuyv);
  add(options.video_device, {1280, 720}, 60, PixelFormat::Mjpeg);
  return configs;
}

std::string make_stats_text(const LoopStats& loop, const CaptureStats& capture, const RenderStats& render,
                            bool vsync, OutputScaling scaling, UpscaleQuality quality,
                            bool opengl_backend, const AudioStats* audio = nullptr) {
  (void)render;
  (void)scaling;
  std::ostringstream out;
  out << std::fixed << std::setprecision(1)
      << "fps " << std::setw(3) << static_cast<int>(std::round(loop.fps))
      << "  drop " << std::setw(5) << capture.dropped
      << "  lat " << std::setw(5) << loop.render_latency_ms << "ms"
      << "  age " << std::setw(5) << loop.frame_age_ms << "ms"
      << "  up " << std::left << std::setw(13) << to_string(quality) << std::right
      << "  " << (opengl_backend ? "gl " : "sdl")
      << "  vs " << (vsync ? "on " : "off");
  if (audio != nullptr) {
    const double latency_ms = audio->sample_rate == 0
                                  ? 0.0
                                  : (static_cast<double>(audio->buffered_frames) * 1000.0 /
                                     static_cast<double>(audio->sample_rate));
    out << "  aud " << std::setw(5) << latency_ms << "ms";
  }
  return out.str();
}

std::vector<std::string> make_gui_lines(const CliOptions& options, const LoopStats& loop, const CaptureStats& capture,
                                        OutputScaling scaling, UpscaleQuality quality, bool opengl_backend,
                                        bool vsync, float rcas_strength, const AudioStats* audio) {
  std::vector<std::string> lines;
  lines.push_back("capture " + options.video_device + " " + std::to_string(options.size.width) + "x" +
                  std::to_string(options.size.height) + " " + std::to_string(options.fps) + "fps " +
                  to_string(options.format));
  lines.push_back("render " + std::string(opengl_backend ? "opengl" : "sdl") +
                  " fps " + std::to_string(static_cast<int>(loop.fps)) +
                  " dropped " + std::to_string(capture.dropped) +
                  " vsync " + (vsync ? "on" : "off"));
  lines.push_back("scaling " + to_string(scaling) + " upscale " + to_string(quality) +
                  " rcas " + std::to_string(rcas_strength));
  lines.push_back("pacing " + options.frame_pacing + " latency-mode " +
                  (options.latency_mode.empty() ? "custom" : options.latency_mode));
  std::ostringstream timing;
  timing << std::fixed << std::setprecision(2)
         << "timing total " << loop.render_latency_ms
         << "ms decode " << loop.decode_ms
         << "ms age " << loop.frame_age_ms
         << "ms jitter " << loop.frame_jitter_ms << "ms";
  lines.push_back(timing.str());
  if (!options.video_output.empty()) {
    lines.push_back("v4l2 output " + options.video_output + " " + options.video_output_format);
  } else {
    lines.push_back("v4l2 output off");
  }
  if (audio != nullptr) {
    lines.push_back("audio monitor on buffered " + std::to_string(audio->buffered_frames) +
                    " underrun " + std::to_string(audio->underruns));
  } else {
    lines.push_back("audio monitor off");
  }
  lines.push_back("press ? for keys");
  return lines;
}

void update_fps(LoopStats& stats) {
  const auto now = Clock::now();
  const double seconds = std::chrono::duration<double>(now - stats.last_report).count();
  if (seconds < 0.5) {
    return;
  }
  stats.fps = static_cast<double>(stats.rendered - stats.rendered_at_report) / seconds;
  stats.rendered_at_report = stats.rendered;
  stats.last_report = now;
}

void record_frame_timing(LoopStats& stats, uint32_t target_fps) {
  const auto now = Clock::now();
  if (stats.last_render.time_since_epoch().count() != 0) {
    stats.frame_interval_ms = elapsed_ms(stats.last_render, now);
    const double target_ms = target_fps == 0 ? 0.0 : 1000.0 / static_cast<double>(target_fps);
    stats.frame_jitter_ms = target_ms == 0.0 ? 0.0 : std::abs(stats.frame_interval_ms - target_ms);
    stats.frame_jitter_stddev_ms = 0.0;
  }
  stats.last_render = now;
}

void maybe_log_runtime_stats(LoopStats& loop, CaptureStats capture, RenderStats render, const AudioStats* audio) {
  const auto now = Clock::now();
  if (std::chrono::duration<double>(now - loop.last_log).count() < 2.0) {
    return;
  }
  loop.last_log = now;
  if (audio != nullptr) {
    const double audio_latency_ms = audio->sample_rate == 0
                                        ? 0.0
                                        : static_cast<double>(audio->buffered_frames) * 1000.0 /
                                              static_cast<double>(audio->sample_rate);
    log::info("runtime fps=", loop.fps,
              " captured=", capture.frames,
              " rendered=", loop.rendered,
              " dropped=", capture.dropped,
              " decode_errors=", loop.decode_errors,
              " decode_ms=", loop.decode_ms,
              " upload_ms=", render.upload_ms,
              " present_ms=", render.present_ms,
              " render_latency_ms=", loop.render_latency_ms,
              " frame_age_ms=", loop.frame_age_ms,
              " frame_ms=", loop.frame_interval_ms,
              " jitter_ms=", loop.frame_jitter_ms,
              " jitter_stddev_ms=", loop.frame_jitter_stddev_ms,
              " audio_ms=", audio_latency_ms,
              " audio_underruns=", audio->underruns,
              " audio_overruns=", audio->overruns,
              " audio_input_frames=", audio->input_frames,
              " audio_output_frames=", audio->output_frames,
              " audio_virtual_frames=", audio->virtual_source_frames);
  } else {
    log::info("runtime fps=", loop.fps,
              " captured=", capture.frames,
              " rendered=", loop.rendered,
              " dropped=", capture.dropped,
              " decode_errors=", loop.decode_errors,
              " decode_ms=", loop.decode_ms,
              " upload_ms=", render.upload_ms,
              " present_ms=", render.present_ms,
              " render_latency_ms=", loop.render_latency_ms,
              " frame_age_ms=", loop.frame_age_ms,
              " frame_ms=", loop.frame_interval_ms,
              " jitter_ms=", loop.frame_jitter_ms,
              " jitter_stddev_ms=", loop.frame_jitter_stddev_ms);
  }
}

RgbaFrame& decode_frame(FrameView frame, MjpegDecoder& mjpeg, RgbaFrame& rgba, LoopStats& stats) {
  const auto start = Clock::now();
  if (frame.format == PixelFormat::Mjpeg) {
    mjpeg.decode(frame, rgba);
  } else if (frame.format == PixelFormat::Yuyv) {
    convert_yuyv_to_rgba(frame, rgba);
  } else if (frame.format == PixelFormat::Nv12) {
    convert_nv12_to_rgba(frame, rgba);
  } else {
    throw AppError("render path supports mjpeg, yuyv, and nv12 only");
  }
  stats.decode_ms = elapsed_ms(start, Clock::now());
  return rgba;
}

void record_render_stats(LoopStats& stats, RenderStats render) {
  stats.render_latency_ms = stats.decode_ms + render.upload_ms + render.present_ms;
}

std::string shell_quote(const std::string& value) {
  if (value.empty()) {
    return "''";
  }
  if (value.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_./:-") ==
      std::string::npos) {
    return value;
  }
  std::string out = "'";
  for (char c : value) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out += c;
    }
  }
  out += "'";
  return out;
}

std::string command_summary(const CliOptions& options) {
  std::ostringstream cmd;
  cmd << "./build/capture-view"
      << " --video " << shell_quote(options.video_device)
      << " --size " << options.size.width << "x" << options.size.height
      << " --fps " << options.fps
      << " --format " << to_string(options.format)
      << " --render-backend " << shell_quote(options.render_backend)
      << " --output-scaling " << options.output_scaling
      << " --upscale-quality " << options.upscale_quality
      << " --rcas-strength " << options.rcas_strength
      << " --frame-pacing " << options.frame_pacing
      << " --audio-buffer-ms " << options.audio_buffer_ms;
  if (!options.video_output.empty()) {
    cmd << " --video-output " << shell_quote(options.video_output);
    cmd << " --video-output-format " << shell_quote(options.video_output_format);
  }
  cmd << (options.vsync ? " --vsync" : " --no-vsync");
  cmd << " --buffers " << options.buffer_count;
  if (!options.latency_mode.empty()) {
    cmd << " --latency-mode " << shell_quote(options.latency_mode);
  }
  if (options.fullscreen) {
    cmd << " --fullscreen";
  }
  if (options.borderless) {
    cmd << " --borderless";
  }
  if (options.audio_monitor) {
    cmd << " --audio-monitor";
    if (!options.audio_input.empty()) {
      cmd << " --audio-input " << shell_quote(options.audio_input);
    }
    if (!options.audio_output.empty()) {
      cmd << " --audio-output " << shell_quote(options.audio_output);
    }
    if (!options.audio_virtual_source.empty()) {
      cmd << " --audio-virtual-source " << shell_quote(options.audio_virtual_source);
    }
  }
  if (!options.profile.empty()) {
    cmd << " --profile " << shell_quote(options.profile);
  }
  return cmd.str();
}

void pace_frame(const CliOptions& options, Clock::time_point& next_frame_at) {
  if (options.frame_pacing == "yield") {
    std::this_thread::yield();
    return;
  }
  if (options.frame_pacing != "sleep" && options.frame_pacing != "adaptive") {
    return;
  }

  const auto now = Clock::now();
  const auto period = options.fps == 0
                          ? std::chrono::milliseconds(16)
                          : std::chrono::duration_cast<Clock::duration>(
                                std::chrono::duration<double>(1.0 / static_cast<double>(options.fps)));
  if (next_frame_at.time_since_epoch().count() == 0 || now > next_frame_at + period) {
    next_frame_at = now;
    return;
  }

  next_frame_at += period;
  if (next_frame_at <= now) {
    return;
  }
  if (options.frame_pacing == "adaptive") {
    const auto yield_at = next_frame_at - std::chrono::milliseconds(1);
    if (yield_at > now) {
      std::this_thread::sleep_until(yield_at);
    }
    while (Clock::now() < next_frame_at) {
      std::this_thread::yield();
    }
  } else {
    std::this_thread::sleep_until(next_frame_at);
  }
}

int run_diagnostic_bundle(const CliOptions& options) {
  std::ofstream out(options.diagnostic_bundle_file);
  if (!out) {
    throw AppError("could not write diagnostic bundle: " + options.diagnostic_bundle_file);
  }
  out << "capture-view diagnostic bundle\n";
  out << "config: " << config_file_path() << "\n";
  out << "command: " << command_summary(options) << "\n\n";
  out << "[video devices]\n";
  for (const auto& device : list_video_devices()) {
    out << device.path << " " << device.card << " [" << device.driver << "]\n";
    for (const auto& format : device.formats) {
      out << "  " << fourcc_to_string(format.fourcc) << " " << format.description << "\n";
      for (const auto& size : format.sizes) {
        out << "    " << size.size.width << "x" << size.size.height;
        for (const auto& interval : size.intervals) {
          if (interval.numerator != 0) {
            out << " " << interval.denominator / interval.numerator << "fps";
          }
        }
        out << "\n";
      }
    }
  }
  out << "\n[audio nodes]\n";
  try {
    for (const auto& device : list_audio_devices()) {
      out << device.id << " " << audio_device_display_name(device)
          << " [" << audio_device_detail(device) << "]\n"
          << "  node: " << device.name << "\n";
    }
  } catch (const AppError& error) {
    out << "PipeWire unavailable: " << error.what() << "\n";
  }
  out << "\n[last config]\n";
  std::ifstream config(config_file_path());
  if (config) {
    out << config.rdbuf();
  } else {
    out << "(no config file)\n";
  }
  log::info("diagnostic bundle written: ", options.diagnostic_bundle_file);
  std::cout << "Wrote diagnostic bundle: " << options.diagnostic_bundle_file << "\n";
  return 0;
}

int run_list_profiles() {
  const auto profiles = list_config_profiles();
  if (profiles.empty()) {
    std::cout << "No profiles in " << config_directory_path() << "\n";
    return 0;
  }
  for (const auto& profile : profiles) {
    std::cout << profile << "\n";
  }
  return 0;
}

int run_init_profiles() {
  init_default_profiles();
  std::cout << "Initialized default profiles in " << config_directory_path() << "\n";
  return run_list_profiles();
}

int run_doctor(const CliOptions& options) {
  log::info("doctor config=", config_file_path(), options.no_config ? " disabled" : "");
  std::cout << "capture-view doctor\n";
  std::cout << "config: " << config_file_path() << (options.no_config ? " (disabled)" : "") << "\n";
  std::cout << "video devices:\n";
  const auto video_devices = list_video_devices();
  log::info("doctor video_device_count=", video_devices.size());
  for (const auto& device : video_devices) {
    log::info("doctor video path=", device.path, " card=", device.card, " driver=", device.driver);
  }
  print_video_devices(video_devices);
  std::cout << "audio nodes:\n";
  try {
    const auto audio_devices = list_audio_devices();
    log::info("doctor audio_node_count=", audio_devices.size());
    for (const auto& device : audio_devices) {
      log::info("doctor audio id=", device.id, " class=", device.media_class, " name=", device.name);
    }
    print_audio_devices(audio_devices);
  } catch (const AppError& error) {
    log::warning("doctor PipeWire unavailable: ", error.what());
    std::cout << "PipeWire unavailable: " << error.what() << "\n";
  }
  std::cout << "SDL_VIDEODRIVER=";
  if (const char* driver = std::getenv("SDL_VIDEODRIVER")) {
    std::cout << driver;
  } else {
    std::cout << "(auto)";
  }
  std::cout << "\n";
  std::cout << "recommended Arch deps: base-devel cmake ninja pipewire sdl3 libjpeg-turbo\n";
  return 0;
}

int run_test_pattern(CliOptions& options) {
  select_audio_devices_if_needed(options);
  SdlRenderer renderer(make_title(options) + " test", options.size, options.fullscreen, options.borderless,
                       options.vsync,
                       output_scaling_from_string(options.output_scaling),
                       upscale_quality_from_string(options.upscale_quality),
                       options.render_backend == "opengl");
  renderer.set_rcas_strength(options.rcas_strength);
  std::unique_ptr<V4l2Output> video_output;
  if (!options.video_output.empty()) {
    video_output = std::make_unique<V4l2Output>(
        V4l2OutputConfig{options.video_output, options.size, options.fps, options.video_output_format});
  }
  std::unique_ptr<AudioMonitor> audio;
  bool muted = options.muted;
  float volume = options.volume;
  if (options.audio_monitor) {
    audio = std::make_unique<AudioMonitor>(
        AudioConfig{options.audio_input,
                    options.audio_output,
                    options.audio_virtual_source,
                    options.audio_buffer_ms,
                    options.audio_quantum,
                    options.audio_delay_ms,
                    volume,
                    muted,
                    options.audio_test_tone});
    audio->start();
  }
  TestPattern pattern(options.size);
  LoopStats stats;
  Clock::time_point next_frame_at{};

  bool running = true;
  while (running) {
    bool restart = false;
    bool audio_restart = false;
    bool mute = false;
    float volume_delta = 0.0F;
    bool scaling_requested = false;
    running = renderer.handle_events(restart, audio_restart, mute, volume_delta, scaling_requested);
    if (mute && audio) {
      muted = !muted;
      audio->set_muted(muted);
      options.muted = muted;
    }
    if (volume_delta != 0.0F && audio) {
      volume = std::clamp(volume + volume_delta, 0.0F, 2.0F);
      audio->set_volume(volume);
      options.volume = volume;
    }
    auto frame = pattern.next();
    if (video_output) {
      video_output->write_frame(frame);
    }
    ++stats.rendered;
    record_frame_timing(stats, options.fps);
    update_fps(stats);
    const AudioStats audio_stats = audio ? audio->stats() : AudioStats{};
    renderer.set_gui_lines(make_gui_lines(options,
                                           stats,
                                           {},
                                           renderer.scaling(),
                                           renderer.upscale_quality(),
                                           renderer.opengl_backend(),
                                           renderer.vsync(),
                                           renderer.rcas_strength(),
                                           audio ? &audio_stats : nullptr));
    renderer.render(frame, make_stats_text(stats, {}, renderer.last_stats(), renderer.vsync(), renderer.scaling(),
                                           renderer.upscale_quality(), renderer.opengl_backend(),
                                           audio ? &audio_stats : nullptr));
    record_render_stats(stats, renderer.last_stats());
    maybe_log_runtime_stats(stats, {}, renderer.last_stats(), audio ? &audio_stats : nullptr);
    pace_frame(options, next_frame_at);
  }
  options.vsync = renderer.vsync();
  options.fullscreen = renderer.fullscreen();
  options.borderless = renderer.borderless();
  options.output_scaling = to_string(renderer.scaling());
  options.upscale_quality = to_string(renderer.upscale_quality());
  options.rcas_strength = renderer.rcas_strength();
  return 0;
}

int run_capture(CliOptions& options) {
  select_audio_devices_if_needed(options);
  std::vector<CaptureConfig> configs = capture_attempts(options);
  if (options.video_device == "/dev/video0") {
    const auto devices = list_video_devices();
    if (!devices.empty() && devices.front().path != options.video_device) {
      log::info("video auto-select: ", devices.front().path);
      CliOptions copy = options;
      copy.video_device = devices.front().path;
      const auto extra = capture_attempts(copy);
      configs.insert(configs.end(), extra.begin(), extra.end());
    }
  }

  std::unique_ptr<V4l2Capture> capture;
  std::string last_error;
  for (const auto& config : configs) {
    try {
      auto candidate = std::make_unique<V4l2Capture>(config);
      log::info("capture attempt device=", config.device,
                " size=", config.size.width, "x", config.size.height,
                " fps=", config.fps,
                " format=", to_string(config.format),
                " buffers=", config.buffer_count);
      candidate->start();
      options.video_device = config.device;
      options.size = candidate->size();
      options.format = candidate->format();
      capture = std::move(candidate);
      break;
    } catch (const AppError& error) {
      last_error = error.what();
      log::warning("capture mode failed: ", config.size.width, "x", config.size.height,
                   " ", config.fps, "fps ", to_string(config.format),
                   ": ", last_error);
    }
  }
  if (!capture) {
    throw AppError("all capture modes failed; last error: " + last_error);
  }

  SdlRenderer renderer(make_title(options), capture->size(), options.fullscreen, options.borderless,
                       options.vsync,
                       output_scaling_from_string(options.output_scaling),
                       upscale_quality_from_string(options.upscale_quality),
                       options.render_backend == "opengl");
  renderer.set_rcas_strength(options.rcas_strength);
  std::unique_ptr<V4l2Output> video_output;
  if (!options.video_output.empty()) {
    video_output = std::make_unique<V4l2Output>(
        V4l2OutputConfig{options.video_output, capture->size(), options.fps, options.video_output_format});
  }
  std::unique_ptr<AudioMonitor> audio;
  bool muted = options.muted;
  float volume = options.volume;
  if (options.audio_monitor) {
    try {
      audio = std::make_unique<AudioMonitor>(
        AudioConfig{options.audio_input,
                    options.audio_output,
                    options.audio_virtual_source,
                    options.audio_buffer_ms,
                    options.audio_quantum,
                    options.audio_delay_ms,
                    volume,
                    muted,
                    options.audio_test_tone});
      audio->start();
    } catch (const AppError& error) {
      log::warning("audio unavailable: ", error.what(), "; continuing video-only");
      audio.reset();
    }
  }
  MjpegDecoder mjpeg;
  RgbaFrame rgba;
  LoopStats stats;
  uint64_t last_tune_underruns = 0;
  uint64_t last_tune_overruns = 0;
  auto last_tune_check = Clock::now();
  Clock::time_point next_frame_at{};

  bool running = true;
  while (running) {
    bool restart = false;
    bool audio_restart = false;
    bool mute_requested = false;
    float volume_delta = 0.0F;
    bool scaling_requested = false;
    running = renderer.handle_events(restart, audio_restart, mute_requested, volume_delta, scaling_requested);
    if (mute_requested && audio) {
      muted = !muted;
      audio->set_muted(muted);
      options.muted = muted;
    }
    if (volume_delta != 0.0F && audio) {
      volume = std::clamp(volume + volume_delta, 0.0F, 2.0F);
      audio->set_volume(volume);
      options.volume = volume;
    }
    if (restart) {
      capture->restart();
      continue;
    }
    if (audio_restart && options.audio_monitor) {
      audio.reset();
      try {
        audio = std::make_unique<AudioMonitor>(
          AudioConfig{options.audio_input,
                      options.audio_output,
                      options.audio_virtual_source,
                      options.audio_buffer_ms,
                      options.audio_quantum,
                      options.audio_delay_ms,
                      volume,
                      muted,
                      options.audio_test_tone});
        audio->start();
        log::info("audio restarted");
      } catch (const AppError& error) {
        log::warning("audio restart failed: ", error.what());
      }
    }

    auto frame = capture->poll_newest(10);
    if (!frame) {
      continue;
    }
    stats.frame_age_ms = frame->captured_at.time_since_epoch().count() == 0
                             ? 0.0
                             : elapsed_ms(frame->captured_at, Clock::now());

    const bool mjpeg_output_passthrough = video_output && options.video_output_format == "mjpeg";
    const bool can_render_raw = !video_output && options.upscale_quality != "bilinear-rcas" &&
                                (frame->format == PixelFormat::Yuyv || frame->format == PixelFormat::Nv12);
    const RgbaFrame* decoded = nullptr;
    try {
      if (!can_render_raw) {
        decoded = &decode_frame(*frame, mjpeg, rgba, stats);
      } else {
        stats.decode_ms = 0.0;
      }
    } catch (const AppError& error) {
      ++stats.decode_errors;
      if (stats.decode_errors <= 3 || stats.decode_errors % 60 == 0) {
        log::warning("dropping undecodable frame sequence=", frame->sequence,
                     " total_decode_errors=", stats.decode_errors,
                     ": ", error.what());
      }
      continue;
    }
    ++stats.rendered;
    if (video_output) {
      if (mjpeg_output_passthrough) {
        video_output->write_frame(*frame);
      } else {
        video_output->write_frame(*decoded);
      }
    }
    record_frame_timing(stats, options.fps);
    update_fps(stats);
    const AudioStats audio_stats = audio ? audio->stats() : AudioStats{};
    if (audio && options.audio_autotune &&
        std::chrono::duration<double>(Clock::now() - last_tune_check).count() >= 3.0) {
      const uint64_t underrun_delta = audio_stats.underruns - last_tune_underruns;
      const uint64_t overrun_delta = audio_stats.overruns - last_tune_overruns;
      last_tune_underruns = audio_stats.underruns;
      last_tune_overruns = audio_stats.overruns;
      last_tune_check = Clock::now();
      if ((underrun_delta > 0 || overrun_delta > 2048) && options.audio_buffer_ms < 20) {
        options.audio_buffer_ms = options.audio_buffer_ms < 15 ? 15 : 20;
        log::warning("audio autotune raised buffer to ", options.audio_buffer_ms,
                     "ms underrun_delta=", underrun_delta,
                     " overrun_delta=", overrun_delta);
        audio.reset();
        try {
          audio = std::make_unique<AudioMonitor>(
            AudioConfig{options.audio_input,
                        options.audio_output,
                        options.audio_virtual_source,
                        options.audio_buffer_ms,
                        options.audio_quantum,
                        options.audio_delay_ms,
                        volume,
                        muted,
                        options.audio_test_tone});
          audio->start();
        } catch (const AppError& error) {
          log::warning("audio autotune restart failed: ", error.what());
        }
      }
    }
    renderer.set_gui_lines(make_gui_lines(options,
                                           stats,
                                           capture->stats(),
                                           renderer.scaling(),
                                           renderer.upscale_quality(),
                                           renderer.opengl_backend(),
                                           renderer.vsync(),
                                           renderer.rcas_strength(),
                                           audio ? &audio_stats : nullptr));
    const std::string stats_text = make_stats_text(stats,
                                                   capture->stats(),
                                                   renderer.last_stats(),
                                                   renderer.vsync(),
                                                   renderer.scaling(),
                                                   renderer.upscale_quality(),
                                                   renderer.opengl_backend(),
                                                   audio ? &audio_stats : nullptr);
    if (can_render_raw) {
      renderer.render(*frame, stats_text);
    } else {
      renderer.render(*decoded, stats_text);
    }
    record_render_stats(stats, renderer.last_stats());
    maybe_log_runtime_stats(stats, capture->stats(), renderer.last_stats(), audio ? &audio_stats : nullptr);
    pace_frame(options, next_frame_at);
  }

  options.vsync = renderer.vsync();
  options.fullscreen = renderer.fullscreen();
  options.borderless = renderer.borderless();
  options.output_scaling = to_string(renderer.scaling());
  options.upscale_quality = to_string(renderer.upscale_quality());
  options.rcas_strength = renderer.rcas_strength();
  return 0;
}

} // namespace

int run_app(CliOptions& options) {
  if (options.gtk_ui) {
    return run_gtk_ui(options);
  }
  if (options.wizard) {
    run_wizard(options);
    std::cout << "\nEquivalent command:\n" << command_summary(options) << "\n\n";
    log::info("wizard command: ", command_summary(options));
  }
  if (options.diagnostic_bundle) {
    return run_diagnostic_bundle(options);
  }
  if (options.list_profiles) {
    return run_list_profiles();
  }
  if (options.init_profiles) {
    return run_init_profiles();
  }
  if (options.doctor) {
    return run_doctor(options);
  }
  if (options.list_devices) {
    print_video_devices(list_video_devices());
    return 0;
  }
  if (options.list_audio) {
    print_audio_devices(list_audio_devices());
    return 0;
  }
  if (options.test_pattern) {
    return run_test_pattern(options);
  }
  return run_capture(options);
}

} // namespace cv
