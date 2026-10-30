#include "audio_monitor.hpp"
#include "common.hpp"

#include <array>
#include <iostream>
#include <pipewire/data-loop.h>

namespace {

void require(bool condition, const char* message) {
  if (!condition) throw cv::AppError(message);
}

struct Stream {
  pw_properties* props = nullptr;
  spa_hook* listener = nullptr;
  const pw_stream_events* events = nullptr;
  void* data = nullptr;
  bool connected = false;
  pw_loop* loop = nullptr;
};

std::array<Stream, 3> streams;
std::byte core_token{};
pw_core* core = reinterpret_cast<pw_core*>(&core_token);
size_t created = 0;
size_t destroyed = 0;
size_t fail_at = 0;
int contexts = 0;
pw_context* shared_context = nullptr;
bool mismatched_loop = false;
int starts = 0;

void test_startup(bool tone, size_t fail, bool mismatch = false) {
  streams = {};
  created = destroyed = 0;
  contexts = 0;
  fail_at = fail;
  mismatched_loop = mismatch;
  starts = 0;
  cv::AudioConfig config;
  config.test_tone = tone;
  config.virtual_source_name = "Capture View";
  bool threw = false;
  {
    cv::AudioMonitor audio(config);
    try { audio.start(); } catch (const cv::AppError&) { threw = true; }
    require(threw == (fail != 0 || mismatch), "startup did not enforce stream allocation and loop identity");
    require(starts == (threw ? 0 : 1), "audio started before its processing loop was verified");
    if (!threw) {
      const size_t expected = tone ? 2U : 3U;
      require(created == expected && contexts == 1, "audio did not use one shared context");
      for (size_t i = 0; i < created; ++i) {
        require(streams[i].connected && streams[i].events != nullptr && streams[i].data == &audio,
                "stream was connected without its callbacks");
        for (size_t j = 0; j < i; ++j) {
          require(streams[i].listener != streams[j].listener, "streams reused a listener hook");
        }
      }
      require(streams[0].events->process == (tone ? cv::AudioMonitor::on_output_process
                                                : cv::AudioMonitor::on_input_process) &&
                  streams[created - 1].events->process == cv::AudioMonitor::on_virtual_source_process,
              "audio callbacks were registered on the wrong streams");
    }
  }
  require(destroyed == created, "audio startup or shutdown leaked a stream");
}

} // namespace

extern "C" pw_context* __real_pw_context_new(pw_loop*, pw_properties*, size_t);

extern "C" pw_context* __wrap_pw_context_new(pw_loop* loop, pw_properties* props, size_t size) {
#if PW_CHECK_VERSION(1, 1, 0)
  if (props == nullptr) props = pw_properties_new(nullptr, nullptr);
  pw_properties_set(props, "context.data-loops",
                    "[ { loop.name = first } { loop.name = second } { loop.name = third } ]");
#endif
  return __real_pw_context_new(loop, props, size);
}

extern "C" pw_core* __wrap_pw_context_connect(pw_context* context, pw_properties*, size_t) {
  ++contexts;
  shared_context = context;
#if PW_CHECK_VERSION(1, 1, 0)
  require(pw_context_get_data_loop(context) != pw_context_get_data_loop(context),
          "test did not configure multiple processing loops");
#endif
  return core;
}

extern "C" int __wrap_pw_core_disconnect(pw_core* value) {
  require(value == core, "disconnected an unexpected core");
  return 0;
}

extern "C" pw_stream* __wrap_pw_stream_new(pw_core* value, const char*, pw_properties* props) {
  require(value == core, "audio streams did not share a core");
#if PW_CHECK_VERSION(1, 1, 0)
  require(pw_properties_get(props, "node.loop.name") != nullptr,
          "audio stream did not select a processing loop");
#endif
  if (created + 1 == fail_at) {
    pw_properties_free(props);
    return nullptr;
  }
  auto& stream = streams.at(created++);
  stream.props = props;
#if PW_CHECK_VERSION(1, 1, 0)
  stream.loop = pw_context_acquire_loop(shared_context, &props->dict);
#else
  stream.loop = pw_data_loop_get_loop(pw_context_get_data_loop(shared_context));
#endif
  require(stream.loop != nullptr && stream.loop == streams[0].loop, "audio streams selected different data loops");
  return reinterpret_cast<pw_stream*>(&stream);
}

extern "C" pw_stream* __wrap_pw_stream_new_simple(pw_loop*, const char*, pw_properties* props,
                                                  const pw_stream_events*, void*) {
  pw_properties_free(props);
  throw cv::AppError("separate stream contexts would race over the audio buffer");
}

extern "C" void __wrap_pw_stream_add_listener(pw_stream* value, spa_hook* listener,
                                               const pw_stream_events* events, void* data) {
  auto& stream = *reinterpret_cast<Stream*>(value);
  stream.listener = listener;
  stream.events = events;
  stream.data = data;
}

extern "C" int __wrap_pw_stream_connect(pw_stream* value, pw_direction, uint32_t, pw_stream_flags flags,
                                         const spa_pod**, uint32_t) {
  require((flags & PW_STREAM_FLAG_RT_PROCESS) != 0, "audio lost real-time processing");
  reinterpret_cast<Stream*>(value)->connected = true;
  return 0;
}

extern "C" void __wrap_pw_stream_destroy(pw_stream* value) {
  pw_properties_free(reinterpret_cast<Stream*>(value)->props);
  ++destroyed;
}

#if PW_CHECK_VERSION(1, 1, 0)
extern "C" pw_loop* __wrap_pw_stream_get_data_loop(pw_stream* value) {
  auto* stream = reinterpret_cast<Stream*>(value);
  return mismatched_loop && stream == &streams[created - 1] ? nullptr : stream->loop;
}
#endif

extern "C" int __wrap_pw_thread_loop_start(pw_thread_loop*) { ++starts; return 0; }

int main() {
  try {
    test_startup(false, 0);
    test_startup(true, 0);
    test_startup(false, 2);
    test_startup(false, 3);
#if PW_CHECK_VERSION(1, 1, 0)
    test_startup(false, 0, true);
#endif
  } catch (const std::exception& error) {
    std::cerr << "test failed: " << error.what() << '\n';
    return 1;
  }
}
