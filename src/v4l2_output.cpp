#include "v4l2_output.hpp"

#include "log.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <span>
#include <sys/ioctl.h>
#include <unistd.h>
#include <utility>

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

bool write_all(int fd, std::span<const std::byte> bytes) {
  const auto* data = reinterpret_cast<const uint8_t*>(bytes.data());
  size_t written = 0;
  while (written < bytes.size()) {
    const ssize_t result = ::write(fd, data + written, bytes.size() - written);
    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return false;
      }
      throw AppError(errno_message("v4l2 output write failed"));
    }
    if (result == 0) {
      throw AppError("v4l2 output write returned 0 bytes");
    }
    written += static_cast<size_t>(result);
  }
  return true;
}

} // namespace

V4l2Output::V4l2Output(V4l2OutputConfig config) : config_(std::move(config)) {
  open_device();
  configure_format();
}

V4l2Output::~V4l2Output() {
  close_device();
}

void V4l2Output::write_frame(const RgbaFrame& frame) {
  if (frame.size.width != config_.size.width || frame.size.height != config_.size.height) {
    throw AppError("v4l2 output frame size changed");
  }
  if (config_.format == "mjpeg") {
    throw AppError("MJPEG v4l2 output requires MJPEG capture input");
  }
  if (config_.format == "yuyv") {
    convert_rgba_to_yuyv(frame, buffer_);
  } else if (config_.format == "nv12") {
    convert_rgba_to_nv12(frame, buffer_);
  } else if (config_.format == "rgba") {
    buffer_ = frame.pixels;
  } else {
    throw AppError("unsupported v4l2 output format: " + config_.format);
  }
  (void)write_all(fd_, buffer_);
}

void V4l2Output::write_frame(FrameView frame) {
  if (frame.size.width != config_.size.width || frame.size.height != config_.size.height) {
    throw AppError("v4l2 output frame size changed");
  }
  if (config_.format == "mjpeg") {
    if (frame.format != PixelFormat::Mjpeg) {
      throw AppError("MJPEG v4l2 output requires MJPEG capture input");
    }
    (void)write_all(fd_, frame.bytes);
    return;
  }
  if (frame.format == PixelFormat::Yuyv) {
    convert_yuyv_to_rgba(frame, rgba_);
  } else if (frame.format == PixelFormat::Nv12) {
    convert_nv12_to_rgba(frame, rgba_);
  } else {
    throw AppError("raw v4l2 output requires decoded frame");
  }
  write_frame(rgba_);
}

void V4l2Output::open_device() {
  fd_ = ::open(config_.device.c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC);
  if (fd_ < 0) {
    throw AppError(errno_message("open v4l2 output " + config_.device + " failed"));
  }

  v4l2_capability cap{};
  if (xioctl(fd_, VIDIOC_QUERYCAP, &cap) != 0) {
    throw AppError(errno_message("VIDIOC_QUERYCAP output failed"));
  }
  const uint32_t caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) != 0 ? cap.device_caps : cap.capabilities;
  if ((caps & V4L2_CAP_VIDEO_OUTPUT) == 0) {
    throw AppError(config_.device + " is not a single-plane V4L2 output device");
  }
  if ((caps & V4L2_CAP_READWRITE) == 0) {
    throw AppError(config_.device + " does not support write() output I/O");
  }
}

void V4l2Output::configure_format() {
  uint32_t fourcc = V4L2_PIX_FMT_YUYV;
  uint32_t bytes_per_pixel = 2;
  uint32_t sizeimage = config_.size.width * config_.size.height * 2;
  if (config_.format == "nv12") {
    fourcc = V4L2_PIX_FMT_NV12;
    bytes_per_pixel = 1;
    sizeimage = config_.size.width * config_.size.height * 3 / 2;
  } else if (config_.format == "rgba") {
    fourcc = V4L2_PIX_FMT_RGB32;
    bytes_per_pixel = 4;
    sizeimage = config_.size.width * config_.size.height * 4;
  } else if (config_.format == "mjpeg") {
    fourcc = V4L2_PIX_FMT_MJPEG;
    bytes_per_pixel = 0;
    sizeimage = config_.size.width * config_.size.height * 2;
  } else if (config_.format != "yuyv") {
    throw AppError("video output format must be yuyv, nv12, rgba, or mjpeg");
  }

  v4l2_format fmt{};
  fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  fmt.fmt.pix.width = config_.size.width;
  fmt.fmt.pix.height = config_.size.height;
  fmt.fmt.pix.pixelformat = fourcc;
  fmt.fmt.pix.field = V4L2_FIELD_NONE;
  fmt.fmt.pix.bytesperline = config_.size.width * bytes_per_pixel;
  fmt.fmt.pix.sizeimage = sizeimage;

  if (xioctl(fd_, VIDIOC_S_FMT, &fmt) != 0) {
    throw AppError(errno_message("VIDIOC_S_FMT output failed"));
  }
  if (fmt.fmt.pix.pixelformat != fourcc) {
    throw AppError("v4l2 output device did not accept " + config_.format);
  }
  if (fmt.fmt.pix.width != config_.size.width || fmt.fmt.pix.height != config_.size.height) {
    throw AppError("v4l2 output device changed requested size");
  }

  v4l2_streamparm parm{};
  parm.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  parm.parm.output.timeperframe.numerator = 1;
  parm.parm.output.timeperframe.denominator = config_.fps;
  (void)xioctl(fd_, VIDIOC_S_PARM, &parm);

  log::info("v4l2 output configured: ", config_.device,
            " ", config_.size.width, "x", config_.size.height,
            " ", config_.fps, "fps ", config_.format);
}

void V4l2Output::close_device() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

} // namespace cv
