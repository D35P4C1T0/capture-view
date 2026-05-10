#pragma once

#include "frame.hpp"

#include <turbojpeg.h>

namespace cv {

class MjpegDecoder {
public:
  MjpegDecoder();
  ~MjpegDecoder();

  MjpegDecoder(const MjpegDecoder&) = delete;
  MjpegDecoder& operator=(const MjpegDecoder&) = delete;

  void decode(FrameView src, RgbaFrame& dst);

private:
  tjhandle handle_ = nullptr;
};

} // namespace cv
