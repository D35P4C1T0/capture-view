#pragma once

#include <atomic>
#include <cstdint>
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <string>
#include <thread>
#include <vector>

namespace cv {

struct AudioConfig {
  std::string input_name;
  std::string output_name;
  std::string virtual_source_name;
  uint32_t buffer_ms = 10;
  uint32_t quantum_frames = 0;
  int32_t delay_ms = 0;
  float volume = 1.0F;
  bool muted = false;
  bool test_tone = false;
};

struct AudioStats {
  uint32_t sample_rate = 48000;
  uint32_t channels = 2;
  uint32_t buffered_frames = 0;
  uint32_t capacity_frames = 0;
  uint64_t underruns = 0;
  uint64_t overruns = 0;
  uint64_t input_frames = 0;
  uint64_t output_frames = 0;
  uint64_t virtual_source_frames = 0;
  int32_t delay_ms = 0;
};

class AudioMonitor {
public:
  explicit AudioMonitor(AudioConfig config);
  ~AudioMonitor();

  AudioMonitor(const AudioMonitor&) = delete;
  AudioMonitor& operator=(const AudioMonitor&) = delete;

  void start();
  void stop();
  void set_muted(bool muted);
  void set_volume(float volume);
  [[nodiscard]] AudioStats stats() const;

  static void on_input_process(void* data);
  static void on_output_process(void* data);
  static void on_virtual_source_process(void* data);
  static void on_param_changed(void* data, uint32_t id, const spa_pod* param);
  static void on_state_changed(void* data, pw_stream_state old_state, pw_stream_state state, const char* error);

private:
  void param_changed(uint32_t id, const spa_pod* param);
  void state_changed(pw_stream_state old_state, pw_stream_state state, const char* error);
  void input_process();
  void output_process();
  void virtual_source_process();
  void write_samples(const float* samples, uint32_t frames);
  void read_samples(float* samples, uint32_t frames);
  void read_samples_for(float* samples, uint32_t frames, uint64_t& read_frame, bool& prebuffering,
                        bool count_underruns);
  void write_test_tone(float* samples, uint32_t frames);
  [[nodiscard]] uint32_t buffered_frames_unlocked() const;
  void publish_stats();

  AudioConfig config_;
  pw_thread_loop* loop_ = nullptr;
  pw_context* context_ = nullptr;
  pw_core* core_ = nullptr;
  pw_stream* input_ = nullptr;
  pw_stream* output_ = nullptr;
  pw_stream* virtual_source_ = nullptr;
  spa_hook input_listener_{};
  spa_hook output_listener_{};

  std::vector<float> ring_;
  uint32_t channels_ = 2;
  uint32_t sample_rate_ = 48000;
  uint32_t capacity_frames_ = 0;
  uint32_t target_delay_frames_ = 0;
  uint32_t quantum_frames_ = 0;
  uint64_t write_frame_ = 0;
  uint64_t read_frame_ = 0;
  uint64_t underruns_ = 0;
  uint64_t overruns_ = 0;
  std::atomic<uint32_t> reported_buffered_frames_{0};
  std::atomic<uint64_t> reported_underruns_{0};
  std::atomic<uint64_t> reported_overruns_{0};
  std::atomic<uint64_t> input_frames_{0};
  std::atomic<uint64_t> output_frames_{0};
  std::atomic<uint64_t> virtual_source_frames_{0};
  double tone_phase_ = 0.0;
  bool prebuffering_ = true;
  bool virtual_prebuffering_ = true;
  uint64_t virtual_read_frame_ = 0;
  std::atomic<float> volume_{1.0F};
  std::atomic_bool muted_{false};
  bool running_ = false;
};

} // namespace cv
