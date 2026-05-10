#include "v4l2_discovery.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <linux/videodev2.h>
#include <optional>
#include <sys/ioctl.h>
#include <unistd.h>

namespace cv {

namespace {

class Fd {
public:
  explicit Fd(const char* path) : fd_(::open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC)) {}
  ~Fd() {
    if (fd_ >= 0) {
      ::close(fd_);
    }
  }
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
  [[nodiscard]] int get() const { return fd_; }

private:
  int fd_ = -1;
};

int xioctl(int fd, unsigned long request, void* arg) {
  int result = 0;
  do {
    result = ::ioctl(fd, request, arg);
  } while (result == -1 && errno == EINTR);
  return result;
}

std::vector<FrameInterval> enum_intervals(int fd, uint32_t fourcc, Size size) {
  std::vector<FrameInterval> intervals;
  v4l2_frmivalenum interval{};
  interval.pixel_format = fourcc;
  interval.width = size.width;
  interval.height = size.height;

  for (interval.index = 0; xioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &interval) == 0; ++interval.index) {
    if (interval.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
      intervals.push_back({interval.discrete.numerator, interval.discrete.denominator});
    } else if (interval.type == V4L2_FRMIVAL_TYPE_STEPWISE) {
      intervals.push_back({interval.stepwise.min.numerator, interval.stepwise.min.denominator});
      intervals.push_back({interval.stepwise.max.numerator, interval.stepwise.max.denominator});
      break;
    }
  }
  return intervals;
}

std::vector<FrameSizeInfo> enum_sizes(int fd, uint32_t fourcc) {
  std::vector<FrameSizeInfo> sizes;
  v4l2_frmsizeenum frame_size{};
  frame_size.pixel_format = fourcc;

  for (frame_size.index = 0; xioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frame_size) == 0; ++frame_size.index) {
    if (frame_size.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
      const Size size{frame_size.discrete.width, frame_size.discrete.height};
      sizes.push_back({size, enum_intervals(fd, fourcc, size)});
    } else if (frame_size.type == V4L2_FRMSIZE_TYPE_STEPWISE) {
      const Size min_size{frame_size.stepwise.min_width, frame_size.stepwise.min_height};
      const Size max_size{frame_size.stepwise.max_width, frame_size.stepwise.max_height};
      sizes.push_back({min_size, enum_intervals(fd, fourcc, min_size)});
      sizes.push_back({max_size, enum_intervals(fd, fourcc, max_size)});
      break;
    }
  }
  return sizes;
}

std::optional<VideoDeviceInfo> inspect_device(const std::filesystem::path& path) {
  Fd fd(path.c_str());
  if (fd.get() < 0) {
    return std::nullopt;
  }

  v4l2_capability cap{};
  if (xioctl(fd.get(), VIDIOC_QUERYCAP, &cap) != 0) {
    return std::nullopt;
  }

  if ((cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0 &&
      (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) == 0) {
    return std::nullopt;
  }

  VideoDeviceInfo info;
  info.path = path.string();
  info.driver = reinterpret_cast<const char*>(cap.driver);
  info.card = reinterpret_cast<const char*>(cap.card);
  info.bus = reinterpret_cast<const char*>(cap.bus_info);

  v4l2_fmtdesc fmt{};
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  for (fmt.index = 0; xioctl(fd.get(), VIDIOC_ENUM_FMT, &fmt) == 0; ++fmt.index) {
    FormatInfo format;
    format.fourcc = fmt.pixelformat;
    format.description = reinterpret_cast<const char*>(fmt.description);
    format.sizes = enum_sizes(fd.get(), fmt.pixelformat);
    info.formats.push_back(std::move(format));
  }

  return info;
}

} // namespace

std::vector<VideoDeviceInfo> list_video_devices() {
  std::vector<VideoDeviceInfo> devices;
  for (const auto& entry : std::filesystem::directory_iterator("/dev")) {
    const std::string name = entry.path().filename().string();
    if (!name.starts_with("video")) {
      continue;
    }
    if (auto info = inspect_device(entry.path())) {
      devices.push_back(std::move(*info));
    }
  }
  std::ranges::sort(devices, {}, &VideoDeviceInfo::path);
  return devices;
}

void print_video_devices(const std::vector<VideoDeviceInfo>& devices) {
  if (devices.empty()) {
    std::cout << "No V4L2 video capture devices found.\n";
    return;
  }
  for (const auto& device : devices) {
    std::cout << device.path << "  " << device.card << "  [" << device.driver << "]\n";
    for (const auto& format : device.formats) {
      std::cout << "  " << fourcc_to_string(format.fourcc) << " (" << format.description << ")\n";
      for (const auto& size : format.sizes) {
        std::cout << "    " << size.size.width << "x" << size.size.height;
        if (!size.intervals.empty()) {
          std::cout << "  ";
        }
        for (size_t i = 0; i < size.intervals.size(); ++i) {
          const auto& interval = size.intervals[i];
          if (interval.numerator != 0) {
            std::cout << interval.denominator / interval.numerator << "fps";
          }
          if (i + 1 < size.intervals.size()) {
            std::cout << ", ";
          }
        }
        std::cout << "\n";
      }
    }
  }
}

} // namespace cv
