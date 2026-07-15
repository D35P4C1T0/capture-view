#include "audio_monitor.hpp"

#include "common.hpp"
#include "log.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <spa/param/props.h>
#include <spa/pod/builder.h>

namespace cv {

namespace {

constexpr spa_audio_info_raw kAudioInfo{
    .format = SPA_AUDIO_FORMAT_F32,
    .flags = 0,
    .rate = 48000,
    .channels = 2,
    .position = {SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR},
};

const pw_stream_events kInputEvents{
    .version = PW_VERSION_STREAM_EVENTS,
    .destroy = nullptr,
    .state_changed = AudioMonitor::on_state_changed,
    .control_info = nullptr,
    .io_changed = nullptr,
    .param_changed = AudioMonitor::on_param_changed,
    .add_buffer = nullptr,
    .remove_buffer = nullptr,
    .process = AudioMonitor::on_input_process,
    .drained = nullptr,
    .command = nullptr,
    .trigger_done = nullptr,
};

const pw_stream_events kOutputEvents{
    .version = PW_VERSION_STREAM_EVENTS,
    .destroy = nullptr,
    .state_changed = AudioMonitor::on_state_changed,
    .control_info = nullptr,
    .io_changed = nullptr,
    .param_changed = AudioMonitor::on_param_changed,
    .add_buffer = nullptr,
    .remove_buffer = nullptr,
    .process = AudioMonitor::on_output_process,
    .drained = nullptr,
    .command = nullptr,
    .trigger_done = nullptr,
};

const pw_stream_events kVirtualSourceEvents{
    .version = PW_VERSION_STREAM_EVENTS,
    .destroy = nullptr,
    .state_changed = AudioMonitor::on_state_changed,
    .control_info = nullptr,
    .io_changed = nullptr,
    .param_changed = AudioMonitor::on_param_changed,
    .add_buffer = nullptr,
    .remove_buffer = nullptr,
    .process = AudioMonitor::on_virtual_source_process,
    .drained = nullptr,
    .command = nullptr,
    .trigger_done = nullptr,
};

} // namespace

static pw_properties* stream_props(const char* category, const char* role, const std::string& target,
                                   uint32_t quantum_frames, uint32_t rate, bool force_quantum) {
  pw_properties* props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, category,
                                           PW_KEY_MEDIA_ROLE, role, PW_KEY_NODE_ALWAYS_PROCESS, "true", nullptr);
  if (!target.empty()) {
    pw_properties_set(props, PW_KEY_TARGET_OBJECT, target.c_str());
  }
  if (quantum_frames > 0) {
    const std::string latency = std::to_string(quantum_frames) + "/" + std::to_string(rate);
    pw_properties_set(props, PW_KEY_NODE_LATENCY, latency.c_str());
    if (force_quantum) {
      const std::string quantum = std::to_string(quantum_frames);
      pw_properties_set(props, PW_KEY_NODE_FORCE_QUANTUM, quantum.c_str());
    }
  }
  return props;
}

static pw_properties* virtual_source_props(const std::string& name, uint32_t quantum_frames, uint32_t rate,
                                           bool force_quantum) {
  pw_properties* props = stream_props("Capture", "Production", {}, quantum_frames, rate, force_quantum);
  const std::string node_name = name.empty() ? "capture-view.monitor" : name;
  pw_properties_set(props, PW_KEY_MEDIA_CLASS, "Audio/Source");
  pw_properties_set(props, PW_KEY_NODE_NAME, node_name.c_str());
  pw_properties_set(props, PW_KEY_NODE_DESCRIPTION, node_name.c_str());
  pw_properties_set(props, PW_KEY_NODE_NICK, "Capture View");
  return props;
}

AudioMonitor::AudioMonitor(AudioConfig config) : config_(std::move(config)) {
  volume_.store(config_.volume);
  muted_.store(config_.muted);
  const auto clamped_delay = std::clamp(config_.delay_ms, -200, 200);
  const uint32_t base_buffer_frames = std::max<uint32_t>(sample_rate_ * std::max(config_.buffer_ms, 1U) / 1000U, 128U);
  const uint32_t extra_delay_frames =
      clamped_delay > 0 ? static_cast<uint32_t>(sample_rate_ * static_cast<uint32_t>(clamped_delay) / 1000U) : 0U;
  target_delay_frames_ = base_buffer_frames + extra_delay_frames;
  capacity_frames_ = std::max<uint32_t>(target_delay_frames_ * 4U, 512U);
  quantum_frames_ =
      config_.quantum_frames == 0 ? std::clamp<uint32_t>(base_buffer_frames / 2U, 128U, 512U) : config_.quantum_frames;
  ring_.resize(static_cast<size_t>(capacity_frames_) * channels_);
  if (clamped_delay < 0) {
    log::warning("audio delay warning: negative delay needs video delay; "
                 "direct monitor keeps audio at minimum latency");
  }
}

AudioMonitor::~AudioMonitor() { stop(); }

void AudioMonitor::start() {
  if (running_) {
    return;
  }

  pw_init(nullptr, nullptr);
  loop_ = pw_thread_loop_new("capture-view-audio", nullptr);
  if (loop_ == nullptr) {
    throw AppError("pw_thread_loop_new failed");
  }
  context_ = pw_context_new(pw_thread_loop_get_loop(loop_), nullptr, 0);
  if (context_ == nullptr) {
    throw AppError("pw_context_new failed");
  }
  core_ = pw_context_connect(context_, nullptr, 0);
  if (core_ == nullptr) {
    throw AppError("pw_context_connect failed");
  }

  input_ = pw_stream_new_simple(pw_thread_loop_get_loop(loop_), "capture-view audio input",
                                stream_props("Capture", "Production", config_.input_name, quantum_frames_, sample_rate_,
                                             config_.quantum_frames != 0),
                                &kInputEvents, this);
  output_ = pw_stream_new_simple(
      pw_thread_loop_get_loop(loop_), "capture-view audio output",
      stream_props("Playback", "Game", config_.output_name, quantum_frames_, sample_rate_, config_.quantum_frames != 0),
      &kOutputEvents, this);
  if (!config_.virtual_source_name.empty()) {
    virtual_source_ = pw_stream_new_simple(
        pw_thread_loop_get_loop(loop_), "capture-view virtual source",
        virtual_source_props(config_.virtual_source_name, quantum_frames_, sample_rate_, config_.quantum_frames != 0),
        &kVirtualSourceEvents, this);
  }
  if ((!config_.test_tone && input_ == nullptr) || output_ == nullptr) {
    throw AppError("pw_stream_new_simple failed");
  }

  std::array<uint8_t, 1024> buffer{};
  spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer.data(), buffer.size());
  spa_audio_info_raw audio_info = kAudioInfo;
  const spa_pod* format = spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &audio_info);
  const spa_pod* params[] = {format};

  uint32_t input_flags = PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS;
  uint32_t output_flags = PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS;

  if (!config_.test_tone) {
    if (pw_stream_connect(input_, PW_DIRECTION_INPUT, PW_ID_ANY, static_cast<pw_stream_flags>(input_flags), params, 1) <
        0) {
      throw AppError("audio input pw_stream_connect failed");
    }
  }
  if (pw_stream_connect(output_, PW_DIRECTION_OUTPUT, PW_ID_ANY, static_cast<pw_stream_flags>(output_flags), params,
                        1) < 0) {
    throw AppError("audio output pw_stream_connect failed");
  }
  if (virtual_source_ != nullptr && pw_stream_connect(virtual_source_, PW_DIRECTION_OUTPUT, PW_ID_ANY,
                                                      static_cast<pw_stream_flags>(output_flags), params, 1) < 0) {
    throw AppError("audio virtual source pw_stream_connect failed");
  }

  if (pw_thread_loop_start(loop_) < 0) {
    throw AppError("pw_thread_loop_start failed");
  }
  running_ = true;
  log::info("audio monitor started: 48kHz stereo F32 buffer=", config_.buffer_ms, "ms delay=", config_.delay_ms,
            "ms input=", config_.input_name, " output=", config_.output_name,
            " virtual_source=", config_.virtual_source_name, " quantum=", quantum_frames_,
            " force_quantum=", config_.quantum_frames != 0 ? "yes" : "no",
            " test_tone=", config_.test_tone ? "yes" : "no");
}

void AudioMonitor::stop() {
  if (!running_ && loop_ == nullptr) {
    return;
  }
  if (loop_ != nullptr) {
    pw_thread_loop_stop(loop_);
  }
  if (input_ != nullptr) {
    pw_stream_destroy(input_);
    input_ = nullptr;
  }
  if (output_ != nullptr) {
    pw_stream_destroy(output_);
    output_ = nullptr;
  }
  if (virtual_source_ != nullptr) {
    pw_stream_destroy(virtual_source_);
    virtual_source_ = nullptr;
  }
  if (core_ != nullptr) {
    pw_core_disconnect(core_);
    core_ = nullptr;
  }
  if (context_ != nullptr) {
    pw_context_destroy(context_);
    context_ = nullptr;
  }
  if (loop_ != nullptr) {
    pw_thread_loop_destroy(loop_);
    loop_ = nullptr;
  }
  running_ = false;
}

void AudioMonitor::set_muted(bool muted) { muted_.store(muted); }

void AudioMonitor::set_volume(float volume) { volume_.store(std::clamp(volume, 0.0F, 2.0F)); }

AudioStats AudioMonitor::stats() const {
  return {sample_rate_,
          channels_,
          reported_buffered_frames_.load(std::memory_order_relaxed),
          capacity_frames_,
          reported_underruns_.load(std::memory_order_relaxed),
          reported_overruns_.load(std::memory_order_relaxed),
          input_frames_.load(),
          output_frames_.load(),
          virtual_source_frames_.load(),
          config_.delay_ms};
}

void AudioMonitor::on_input_process(void* data) { static_cast<AudioMonitor*>(data)->input_process(); }

void AudioMonitor::on_output_process(void* data) { static_cast<AudioMonitor*>(data)->output_process(); }

void AudioMonitor::on_virtual_source_process(void* data) { static_cast<AudioMonitor*>(data)->virtual_source_process(); }

void AudioMonitor::on_param_changed(void* data, uint32_t id, const spa_pod* param) {
  static_cast<AudioMonitor*>(data)->param_changed(id, param);
}

void AudioMonitor::on_state_changed(void* data, pw_stream_state old_state, pw_stream_state state, const char* error) {
  static_cast<AudioMonitor*>(data)->state_changed(old_state, state, error);
}

void AudioMonitor::state_changed(pw_stream_state old_state, pw_stream_state state, const char* error) {
  log::info("audio stream state: ", pw_stream_state_as_string(old_state), " -> ", pw_stream_state_as_string(state),
            error != nullptr ? " error=" : "", error != nullptr ? error : "");
}

void AudioMonitor::param_changed(uint32_t id, const spa_pod* param) {
  if (id != SPA_PARAM_Format || param == nullptr) {
    return;
  }

  spa_audio_info info{};
  if (spa_format_parse(param, &info.media_type, &info.media_subtype) < 0 || info.media_type != SPA_MEDIA_TYPE_audio ||
      info.media_subtype != SPA_MEDIA_SUBTYPE_raw || spa_format_audio_raw_parse(param, &info.info.raw) < 0) {
    return;
  }

  if (info.info.raw.rate != sample_rate_ || info.info.raw.channels != channels_) {
    log::info("audio negotiated: ", info.info.raw.rate, "Hz channels=", info.info.raw.channels,
              " format-id=", info.info.raw.format);
  }
}

void AudioMonitor::input_process() {
  pw_buffer* buffer = pw_stream_dequeue_buffer(input_);
  if (buffer == nullptr) {
    return;
  }
  spa_buffer* spa_buffer = buffer->buffer;
  if (spa_buffer->n_datas > 0 && spa_buffer->datas[0].data != nullptr) {
    const auto* samples = static_cast<const float*>(spa_buffer->datas[0].data);
    const uint32_t bytes = spa_buffer->datas[0].chunk->size;
    const uint32_t frames = static_cast<uint32_t>(bytes / (sizeof(float) * channels_));
    write_samples(samples, frames);
    input_frames_.fetch_add(frames);
  }
  pw_stream_queue_buffer(input_, buffer);
}

void AudioMonitor::output_process() {
  pw_buffer* buffer = pw_stream_dequeue_buffer(output_);
  if (buffer == nullptr) {
    return;
  }
  spa_buffer* spa_buffer = buffer->buffer;
  if (spa_buffer->n_datas > 0 && spa_buffer->datas[0].data != nullptr) {
    auto* samples = static_cast<float*>(spa_buffer->datas[0].data);
    const uint32_t max_bytes = spa_buffer->datas[0].maxsize;
    const uint32_t max_frames = static_cast<uint32_t>(max_bytes / (sizeof(float) * channels_));
    const uint32_t frames = std::min<uint32_t>(max_frames, quantum_frames_);
    if (config_.test_tone) {
      write_test_tone(samples, frames);
    } else {
      read_samples(samples, frames);
    }
    output_frames_.fetch_add(frames);
    spa_buffer->datas[0].chunk->offset = 0;
    spa_buffer->datas[0].chunk->stride = static_cast<int32_t>(sizeof(float) * channels_);
    spa_buffer->datas[0].chunk->size = frames * channels_ * sizeof(float);
  }
  pw_stream_queue_buffer(output_, buffer);
}

void AudioMonitor::virtual_source_process() {
  pw_buffer* buffer = pw_stream_dequeue_buffer(virtual_source_);
  if (buffer == nullptr) {
    return;
  }
  spa_buffer* spa_buffer = buffer->buffer;
  if (spa_buffer->n_datas > 0 && spa_buffer->datas[0].data != nullptr) {
    auto* samples = static_cast<float*>(spa_buffer->datas[0].data);
    const uint32_t max_bytes = spa_buffer->datas[0].maxsize;
    const uint32_t max_frames = static_cast<uint32_t>(max_bytes / (sizeof(float) * channels_));
    const uint32_t frames = std::min<uint32_t>(max_frames, quantum_frames_);
    if (config_.test_tone) {
      write_test_tone(samples, frames);
    } else {
      read_samples_for(samples, frames, virtual_read_frame_, virtual_prebuffering_, false);
    }
    virtual_source_frames_.fetch_add(frames);
    spa_buffer->datas[0].chunk->offset = 0;
    spa_buffer->datas[0].chunk->stride = static_cast<int32_t>(sizeof(float) * channels_);
    spa_buffer->datas[0].chunk->size = frames * channels_ * sizeof(float);
  }
  pw_stream_queue_buffer(virtual_source_, buffer);
}

void AudioMonitor::write_samples(const float* samples, uint32_t frames) {
  for (uint32_t frame = 0; frame < frames; ++frame) {
    const uint64_t oldest = virtual_source_ == nullptr ? read_frame_ : std::min(read_frame_, virtual_read_frame_);
    if (write_frame_ - oldest >= capacity_frames_) {
      const uint64_t buffered = write_frame_ - oldest;
      const uint64_t keep = std::min<uint64_t>(target_delay_frames_, capacity_frames_ / 2U);
      const uint64_t drop = buffered > keep ? buffered - keep : 1U;
      const uint64_t new_oldest = oldest + drop;
      read_frame_ = std::max(read_frame_, new_oldest);
      virtual_read_frame_ = std::max(virtual_read_frame_, new_oldest);
      overruns_ += drop;
    }
    const uint64_t ring_frame = write_frame_ % capacity_frames_;
    for (uint32_t ch = 0; ch < channels_; ++ch) {
      ring_[static_cast<size_t>(ring_frame) * channels_ + ch] = samples[static_cast<size_t>(frame) * channels_ + ch];
    }
    ++write_frame_;
  }
  publish_stats();
}

void AudioMonitor::read_samples(float* samples, uint32_t frames) {
  read_samples_for(samples, frames, read_frame_, prebuffering_, true);
  publish_stats();
}

void AudioMonitor::read_samples_for(float* samples, uint32_t frames, uint64_t& read_frame, bool& prebuffering,
                                    bool count_underruns) {
  const float volume = muted_.load() ? 0.0F : volume_.load();
  if (prebuffering) {
    if (write_frame_ - read_frame < target_delay_frames_) {
      std::memset(samples, 0, static_cast<size_t>(frames) * channels_ * sizeof(float));
      return;
    }
    prebuffering = false;
  }

  for (uint32_t frame = 0; frame < frames; ++frame) {
    if (read_frame >= write_frame_) {
      std::memset(samples + static_cast<size_t>(frame) * channels_, 0, channels_ * sizeof(float));
      if (count_underruns) {
        ++underruns_;
      }
      prebuffering = true;
      continue;
    }
    const uint64_t ring_frame = read_frame % capacity_frames_;
    for (uint32_t ch = 0; ch < channels_; ++ch) {
      samples[static_cast<size_t>(frame) * channels_ + ch] =
          ring_[static_cast<size_t>(ring_frame) * channels_ + ch] * volume;
    }
    ++read_frame;
  }
}

void AudioMonitor::write_test_tone(float* samples, uint32_t frames) {
  const float volume = muted_.load() ? 0.0F : volume_.load() * 0.20F;
  constexpr double frequency = 440.0;
  constexpr double two_pi = 6.283185307179586;
  for (uint32_t frame = 0; frame < frames; ++frame) {
    const float sample = static_cast<float>(std::sin(tone_phase_) * volume);
    tone_phase_ += two_pi * frequency / static_cast<double>(sample_rate_);
    if (tone_phase_ >= two_pi) {
      tone_phase_ -= two_pi;
    }
    for (uint32_t ch = 0; ch < channels_; ++ch) {
      samples[static_cast<size_t>(frame) * channels_ + ch] = sample;
    }
  }
}

uint32_t AudioMonitor::buffered_frames_unlocked() const {
  return static_cast<uint32_t>(std::min<uint64_t>(write_frame_ - read_frame_, capacity_frames_));
}

void AudioMonitor::publish_stats() {
  reported_buffered_frames_.store(buffered_frames_unlocked(), std::memory_order_relaxed);
  reported_underruns_.store(underruns_, std::memory_order_relaxed);
  reported_overruns_.store(overruns_, std::memory_order_relaxed);
}

} // namespace cv
