#include "audio_devices.hpp"
#include "common.hpp"
#include "v4l2_util.hpp"

#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>

namespace {

void require(bool condition, const char* message) {
  if (!condition) throw cv::AppError(message);
}

void test_discovery_failure(bool stall) {
  std::array<char, 64> directory{};
  std::strcpy(directory.data(), "/tmp/capture-view-discovery-test-XXXXXX");
  require(mkdtemp(directory.data()) != nullptr, "could not create discovery test directory");
  const std::string path = std::string(directory.data()) + "/pipewire";
  cv::UniqueFd listener(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
  require(static_cast<bool>(listener), "could not create test socket");
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  std::strcpy(address.sun_path, path.c_str());
  require(bind(listener.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 &&
              listen(listener.get(), 1) == 0, "could not listen on test socket");
  const char* old_remote = std::getenv("PIPEWIRE_REMOTE");
  const std::optional<std::string> previous = old_remote == nullptr ? std::nullopt : std::optional<std::string>(old_remote);
  setenv("PIPEWIRE_REMOTE", path.c_str(), 1);
  std::thread server([&] {
    pollfd pending{listener.get(), POLLIN, 0};
    if (poll(&pending, 1, 7000) <= 0) return;
    cv::UniqueFd connection(accept4(listener.get(), nullptr, nullptr, SOCK_CLOEXEC));
    if (!connection || !stall) return;
    const timeval timeout{8, 0};
    setsockopt(connection.get(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    std::array<char, 4096> bytes{};
    while (read(connection.get(), bytes.data(), bytes.size()) > 0) {}
  });
  const auto started = cv::Clock::now();
  std::string error;
  try { (void)cv::list_audio_devices(); } catch (const cv::AppError& e) { error = e.what(); }
  const double elapsed = std::chrono::duration<double>(cv::Clock::now() - started).count();
  server.join();
  if (previous) setenv("PIPEWIRE_REMOTE", previous->c_str(), 1);
  else unsetenv("PIPEWIRE_REMOTE");
  listener.reset();
  unlink(path.c_str());
  rmdir(directory.data());
  require(!error.empty(), "discovery did not report the failed connection");
  if (stall) {
    require(error.find("timed out") != std::string::npos && elapsed >= 4.0 && elapsed < 7.0,
            "stalled discovery did not stop at its deadline");
  } else {
    require(elapsed < 4.0, "discovery waited for its deadline after a connection error");
  }
}

} // namespace

int main() {
  try {
    test_discovery_failure(false);
    test_discovery_failure(true);
  } catch (const std::exception& error) {
    std::cerr << "test failed: " << error.what() << '\n';
    return 1;
  }
}
