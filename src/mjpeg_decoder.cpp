#include "mjpeg_decoder.hpp"

#include <string>

namespace cv {

MjpegDecoder::MjpegDecoder() : handle_(tjInitDecompress()) {
  if (handle_ == nullptr) {
    throw AppError("tjInitDecompress failed");
  }
}

MjpegDecoder::~MjpegDecoder() {
  if (handle_ != nullptr) {
    tjDestroy(handle_);
  }
}

void MjpegDecoder::decode(FrameView src, RgbaFrame& dst) {
  int width = 0;
  int height = 0;
  int subsamp = 0;
  int colorspace = 0;

  const auto* jpeg = reinterpret_cast<const unsigned char*>(src.bytes.data());
  const auto jpeg_size = static_cast<unsigned long>(src.bytes.size());

  if (tjDecompressHeader3(handle_, jpeg, jpeg_size, &width, &height, &subsamp, &colorspace) != 0) {
    throw AppError(std::string("MJPEG header decode failed: ") + tjGetErrorStr2(handle_));
  }

  dst.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
  dst.sequence = src.sequence;
  dst.pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);

  if (tjDecompress2(handle_, jpeg, jpeg_size,
                    reinterpret_cast<unsigned char*>(dst.pixels.data()),
                    width,
                    width * 4,
                    height,
                    TJPF_RGBA,
                    TJFLAG_FASTDCT | TJFLAG_FASTUPSAMPLE) != 0) {
    throw AppError(std::string("MJPEG decode failed: ") + tjGetErrorStr2(handle_));
  }
}

} // namespace cv
