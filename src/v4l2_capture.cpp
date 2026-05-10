#include "v4l2_capture.hpp"

#include "log.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace cv {

namespace {

int xioctl(int fd, unsigned long request, void* arg) {
  int result = 0;
  do {
    result = ::ioctl(fd, request, arg);
  } while (result == -1 && errno == EINTR);
  return result;
}

std::string errno_message(const std::string& prefix) {
  return prefix + ": " + std::strerror(errno);
}

} // namespace

V4l2Capture::V4l2Capture(CaptureConfig config) : config_(std::move(config)) {}

V4l2Capture::~V4l2Capture() {
  stop();
  destroy_buffers();
  close_device();
}

void V4l2Capture::start() {
  open_device();
  configure_format();
  create_buffers();
  queue_all_buffers();

  v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (xioctl(fd_, VIDIOC_STREAMON, &type) != 0) {
    throw AppError(errno_message("VIDIOC_STREAMON failed"));
  }
  streaming_ = true;
}

void V4l2Capture::stop() {
  if (!streaming_ || fd_ < 0) {
    return;
  }
  if (held_buffer_) {
    requeue(*held_buffer_);
    held_buffer_.reset();
  }
  v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  (void)xioctl(fd_, VIDIOC_STREAMOFF, &type);
  streaming_ = false;
}

void V4l2Capture::restart() {
  stop();
  destroy_buffers();
  close_device();
  stats_ = {};
  sequence_ = 0;
  start();
}

void V4l2Capture::open_device() {
  if (fd_ >= 0) {
    return;
  }
  fd_ = ::open(config_.device.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
  if (fd_ < 0) {
    if (errno == EBUSY) {
      throw AppError("video device busy: " + config_.device);
    }
    throw AppError(errno_message("open " + config_.device + " failed"));
  }

  v4l2_capability cap{};
  if (xioctl(fd_, VIDIOC_QUERYCAP, &cap) != 0) {
    throw AppError(errno_message("VIDIOC_QUERYCAP failed"));
  }
  if ((cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0) {
    throw AppError(config_.device + " is not a single-plane V4L2 capture device");
  }
  if ((cap.capabilities & V4L2_CAP_STREAMING) == 0) {
    throw AppError(config_.device + " does not support streaming I/O");
  }
}

void V4l2Capture::configure_format() {
  const uint32_t fourcc = pixel_format_to_v4l2(config_.format);
  if (fourcc == 0) {
    throw AppError("unsupported capture pixel format");
  }

  v4l2_format fmt{};
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = config_.size.width;
  fmt.fmt.pix.height = config_.size.height;
  fmt.fmt.pix.pixelformat = fourcc;
  fmt.fmt.pix.field = V4L2_FIELD_ANY;

  if (xioctl(fd_, VIDIOC_S_FMT, &fmt) != 0) {
    throw AppError(errno_message("VIDIOC_S_FMT failed"));
  }

  v4l2_streamparm parm{};
  parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  parm.parm.capture.timeperframe.numerator = 1;
  parm.parm.capture.timeperframe.denominator = config_.fps;
  (void)xioctl(fd_, VIDIOC_S_PARM, &parm);

  size_ = {fmt.fmt.pix.width, fmt.fmt.pix.height};
  format_ = pixel_format_from_v4l2(fmt.fmt.pix.pixelformat);
  if (format_ == PixelFormat::Unknown || (config_.format != PixelFormat::Auto && format_ != config_.format)) {
    throw AppError("driver negotiated unsupported format " + fourcc_to_string(fmt.fmt.pix.pixelformat));
  }
  log::info("video negotiated: ", size_.width, "x", size_.height, " ",
            fourcc_to_string(fmt.fmt.pix.pixelformat),
            " bytesperline=", fmt.fmt.pix.bytesperline,
            " sizeimage=", fmt.fmt.pix.sizeimage);
}

void V4l2Capture::create_buffers() {
  v4l2_requestbuffers req{};
  req.count = config_.buffer_count;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;
  if (xioctl(fd_, VIDIOC_REQBUFS, &req) != 0) {
    throw AppError(errno_message("VIDIOC_REQBUFS failed"));
  }
  if (req.count < 2) {
    throw AppError("driver could not allocate at least 2 capture buffers");
  }

  buffers_.resize(req.count);
  for (uint32_t i = 0; i < req.count; ++i) {
    v4l2_buffer buf{};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = i;
    if (xioctl(fd_, VIDIOC_QUERYBUF, &buf) != 0) {
      throw AppError(errno_message("VIDIOC_QUERYBUF failed"));
    }

    void* start = ::mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, buf.m.offset);
    if (start == MAP_FAILED) {
      throw AppError(errno_message("mmap capture buffer failed"));
    }
    buffers_[i] = {start, buf.length};
  }
}

void V4l2Capture::queue_all_buffers() {
  for (uint32_t i = 0; i < buffers_.size(); ++i) {
    requeue(i);
  }
}

void V4l2Capture::destroy_buffers() {
  for (auto& buffer : buffers_) {
    if (buffer.start != nullptr) {
      ::munmap(buffer.start, buffer.length);
      buffer = {};
    }
  }
  buffers_.clear();
}

void V4l2Capture::close_device() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

void V4l2Capture::requeue(uint32_t index) {
  v4l2_buffer buf{};
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;
  buf.index = index;
  if (xioctl(fd_, VIDIOC_QBUF, &buf) != 0) {
    throw AppError(errno_message("VIDIOC_QBUF failed"));
  }
}

std::optional<FrameView> V4l2Capture::poll_newest(int timeout_ms) {
  if (held_buffer_) {
    requeue(*held_buffer_);
    held_buffer_.reset();
  }

  pollfd pfd{};
  pfd.fd = fd_;
  pfd.events = POLLIN;
  const int ready = ::poll(&pfd, 1, timeout_ms);
  if (ready < 0) {
    if (errno == EINTR) {
      return std::nullopt;
    }
    throw AppError(errno_message("poll failed"));
  }
  if (ready == 0) {
    return std::nullopt;
  }

  std::optional<v4l2_buffer> newest;
  while (true) {
    v4l2_buffer buf{};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd_, VIDIOC_DQBUF, &buf) != 0) {
      if (errno == EAGAIN) {
        break;
      }
      throw AppError(errno_message("VIDIOC_DQBUF failed"));
    }

    ++stats_.frames;
    if (newest) {
      requeue(newest->index);
      ++stats_.dropped;
    }
    newest = buf;
  }

  if (!newest) {
    return std::nullopt;
  }

  held_buffer_ = newest->index;
  const Buffer& buffer = buffers_.at(newest->index);
  return FrameView{
      std::span<const std::byte>(reinterpret_cast<const std::byte*>(buffer.start), newest->bytesused),
      format_,
      size_,
      ++sequence_,
      Clock::now(),
  };
}

} // namespace cv
