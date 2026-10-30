#pragma once

#include "frame.hpp"

#include <memory>

namespace cv {

class H264Decoder {
public:
  H264Decoder();
  ~H264Decoder();

  H264Decoder(const H264Decoder&) = delete;
  H264Decoder& operator=(const H264Decoder&) = delete;

  // Returns false while the decoder is waiting for an initial complete frame.
  bool decode(FrameView src, RgbaFrame& dst);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace cv
