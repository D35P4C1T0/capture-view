#include "audio_monitor.hpp"
#include "config.hpp"
#include "v4l2_capture.hpp"

#include <array>
#include <cstdarg>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iostream>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/resource.h>
#include <sys/wait.h>

namespace {

void require(bool condition, const char* message) {
  if (!condition) throw cv::AppError(message);
}

pw_buffer audio_buffer{};
std::array<std::array<std::byte, 16>, 2> video_buffers{};
bool dequeued = false;
bool disconnected = false;
bool stopped = false;
int queues = 0;
int unmapped = 0;
uint32_t capture_bytes = 16;
bool set_fps_fails = false;
v4l2_fract negotiated_interval{1, 30};

void test_audio_requested_frames() {
  std::array<float, 2048> samples{};
  spa_chunk chunk{};
  spa_data data{};
  data.data = samples.data();
  data.maxsize = sizeof(samples);
  data.chunk = &chunk;
  spa_buffer buffer{};
  buffer.n_datas = 1;
  buffer.datas = &data;
  audio_buffer = {};
  audio_buffer.buffer = &buffer;
  cv::AudioMonitor audio({});
  for (auto callback : {cv::AudioMonitor::on_output_process, cv::AudioMonitor::on_virtual_source_process}) {
    audio_buffer.requested = 1024;
    callback(&audio);
    require(chunk.size == 8192 && audio_buffer.size == 1024, "audio ignored the server request");
    audio_buffer.requested = UINT64_MAX;
    callback(&audio);
    require(chunk.size == sizeof(samples), "audio request exceeded buffer capacity");
    audio_buffer.requested = 0;
    callback(&audio);
    require(chunk.size == 240 * 8, "audio fallback processing size changed");
    data.chunk = nullptr;
    callback(&audio);
    data.chunk = &chunk;
  }
}

void test_audio_input_offset_and_bounds() {
  std::array<float, 512> samples{};
  samples[510] = 0.7F;
  samples[511] = 0.8F;
  samples[0] = 0.3F;
  samples[1] = 0.4F;
  spa_chunk chunk{};
  chunk.offset = sizeof(samples) + 255 * 8;
  chunk.size = 128 * 8;
  spa_data data{};
  data.data = samples.data();
  data.maxsize = sizeof(samples);
  data.chunk = &chunk;
  spa_buffer buffer{};
  buffer.n_datas = 1;
  buffer.datas = &data;
  audio_buffer = {};
  audio_buffer.buffer = &buffer;
  audio_buffer.requested = 128;
  cv::AudioConfig config;
  config.buffer_ms = 1;
  cv::AudioMonitor audio(config);
  cv::AudioMonitor::on_input_process(&audio);
  cv::AudioMonitor::on_output_process(&audio);
  require(samples[0] == 0.7F && samples[1] == 0.8F && samples[2] == 0.3F && samples[3] == 0.4F,
          "audio did not honor the chunk offset and wrapped data");
  const auto input_frames = audio.stats().input_frames;
  chunk.offset = 0;
  chunk.size = UINT32_MAX;
  cv::AudioMonitor::on_input_process(&audio);
  require(audio.stats().input_frames == input_frames + 256, "audio did not clamp input size to capacity");
  std::array<float, 256> unaligned{};
  unaligned[0] = 0.25F;
  unaligned[1] = 0.5F;
  std::memcpy(reinterpret_cast<std::byte*>(samples.data()) + 1, unaligned.data(), sizeof(unaligned));
  data.maxsize = sizeof(samples) - 1;
  chunk.offset = 1;
  chunk.size = sizeof(unaligned);
  cv::AudioMonitor unaligned_audio(config);
  cv::AudioMonitor::on_input_process(&unaligned_audio);
  cv::AudioMonitor::on_output_process(&unaligned_audio);
  require(samples[0] == 0.25F && samples[1] == 0.5F, "unaligned input was read incorrectly");
  data.maxsize = 0;
  cv::AudioMonitor::on_input_process(&audio);
  data.chunk = nullptr;
  cv::AudioMonitor::on_input_process(&audio);
}

void test_capture_cleanup_after_disconnect() {
  video_buffers[0] = {std::byte{16}, std::byte{128}, std::byte{16}, std::byte{128},
                      std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
                      std::byte{235}, std::byte{128}, std::byte{235}, std::byte{128}};
  {
    cv::V4l2Capture capture({"/dev/null", {2, 2}, 60, cv::PixelFormat::Yuyv, 2});
    capture.start();
    require(capture.fps() == 30, "capture ignored the driver frame rate");
    capture_bytes = 3;
    require(!capture.poll_newest(0), "short raw capture frame was not dropped");
    require(capture.stats().dropped == 1, "invalid raw frame was not counted");
    capture_bytes = 16;
    dequeued = false;
    auto frame = capture.poll_newest(0);
    require(frame && frame->row_stride == 8, "capture lost negotiated row spacing");
    cv::RgbaFrame rgba;
    cv::convert_yuyv_to_rgba(*frame, rgba);
    require(rgba.pixels[8] == std::byte{255}, "capture did not preserve the second row");
    disconnected = true;
  }
  require(queues == 3 && stopped && unmapped == 2, "disconnected capture was not cleaned up safely");
}

void test_capture_frame_rate_fallback() {
  disconnected = false;
  set_fps_fails = true;
  {
    cv::V4l2Capture capture({"/dev/null", {2, 2}, 24, cv::PixelFormat::Yuyv, 2});
    capture.start();
    require(capture.fps() == 24, "unsupported rate negotiation lost the candidate frame rate");
  }
  set_fps_fails = false;
  for (const auto interval : {v4l2_fract{0, 0}, v4l2_fract{1001, 60000}}) {
    negotiated_interval = interval;
    cv::V4l2Capture capture({"/dev/null", {2, 2}, 60, cv::PixelFormat::Yuyv, 2});
    capture.start();
    require(capture.fps() == 60, "invalid or fractional driver interval produced the wrong FPS");
  }
}

void test_failed_config_write_preserves_original() {
  std::array<char, 64> directory{};
  std::strcpy(directory.data(), "/tmp/capture-view-write-test-XXXXXX");
  require(mkdtemp(directory.data()) != nullptr, "could not create config test directory");
  const std::string path = std::string(directory.data()) + "/config.toml";
  { std::ofstream original(path); original << "original\n"; }
  const pid_t child = fork();
  require(child >= 0, "fork failed");
  if (child == 0) {
    signal(SIGXFSZ, SIG_IGN);
    const rlimit limit{32, 32};
    if (setrlimit(RLIMIT_FSIZE, &limit) != 0) _exit(2);
    cv::CliOptions options;
    options.config_file = path;
    cv::set_config_location(options);
    try { cv::save_config(options); } catch (const std::exception&) { _exit(0); }
    _exit(1);
  }
  int status = 0;
  require(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "failed config write was not reported");
  std::ifstream original(path);
  std::string contents;
  std::getline(original, contents);
  require(contents == "original", "failed write damaged the original config");
  original.close();
  require(unlink(path.c_str()) == 0 && rmdir(directory.data()) == 0, "failed save left temporary files");
}

} // namespace

extern "C" pw_buffer* __wrap_pw_stream_dequeue_buffer(pw_stream*) { return &audio_buffer; }
extern "C" int __wrap_pw_stream_queue_buffer(pw_stream*, pw_buffer*) { return 0; }

extern "C" int __wrap_ioctl(int, unsigned long request, ...) {
  va_list args;
  va_start(args, request);
  void* arg = va_arg(args, void*);
  va_end(args);
  switch (request) {
  case VIDIOC_QUERYCAP:
    static_cast<v4l2_capability*>(arg)->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
    return 0;
  case VIDIOC_S_FMT:
    static_cast<v4l2_format*>(arg)->fmt.pix.bytesperline = 8;
    return 0;
  case VIDIOC_S_PARM:
    if (set_fps_fails) { errno = EINVAL; return -1; }
    static_cast<v4l2_streamparm*>(arg)->parm.capture.timeperframe = negotiated_interval;
    return 0;
  case VIDIOC_REQBUFS: return 0;
  case VIDIOC_QUERYBUF: {
    auto* buffer = static_cast<v4l2_buffer*>(arg);
    buffer->length = 16;
    buffer->m.offset = buffer->index * 16;
    return 0;
  }
  case VIDIOC_QBUF:
    ++queues;
    if (disconnected) { errno = ENODEV; return -1; }
    return 0;
  case VIDIOC_STREAMON: return 0;
  case VIDIOC_STREAMOFF:
    stopped = true;
    errno = ENODEV;
    return -1;
  case VIDIOC_DQBUF:
    if (dequeued) { errno = EAGAIN; return -1; }
    dequeued = true;
    static_cast<v4l2_buffer*>(arg)->index = 0;
    static_cast<v4l2_buffer*>(arg)->bytesused = capture_bytes;
    return 0;
  default: errno = EINVAL; return -1;
  }
}
extern "C" void* __wrap_mmap(void*, size_t, int, int, int, off_t offset) {
  return video_buffers.at(static_cast<size_t>(offset) / 16).data();
}
extern "C" int __wrap_munmap(void*, size_t) { ++unmapped; return 0; }
extern "C" int __wrap_poll(pollfd*, nfds_t, int) { return 1; }

int main() {
  try {
    test_audio_requested_frames();
    test_audio_input_offset_and_bounds();
    test_capture_cleanup_after_disconnect();
    test_capture_frame_rate_fallback();
    test_failed_config_write_preserves_original();
  } catch (const std::exception& error) {
    std::cerr << "test failed: " << error.what() << '\n';
    return 1;
  }
}
