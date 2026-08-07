#include "h264_decoder.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libswscale/swscale.h>
}

#include <array>
#include <cstring>
#include <limits>

namespace cv {

namespace {

std::string ffmpeg_error(int code) {
  std::array<char, AV_ERROR_MAX_STRING_SIZE> text{};
  av_strerror(code, text.data(), text.size());
  return text.data();
}

} // namespace

struct H264Decoder::Impl {
  AVCodecContext* context = nullptr;
  AVFrame* frame = nullptr;
  AVPacket* packet = nullptr;
  SwsContext* scaler = nullptr;

  ~Impl() {
    sws_freeContext(scaler);
    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&context);
  }
};

H264Decoder::H264Decoder() : impl_(std::make_unique<Impl>()) {
  const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
  if (codec == nullptr) {
    throw AppError("FFmpeg H.264 decoder is unavailable");
  }
  impl_->context = avcodec_alloc_context3(codec);
  impl_->frame = av_frame_alloc();
  impl_->packet = av_packet_alloc();
  if (impl_->context == nullptr || impl_->frame == nullptr || impl_->packet == nullptr) {
    throw AppError("could not allocate H.264 decoder");
  }

  impl_->context->flags |= AV_CODEC_FLAG_LOW_DELAY;
  impl_->context->flags2 |= AV_CODEC_FLAG2_FAST;
  impl_->context->thread_type = FF_THREAD_SLICE;
  impl_->context->thread_count = 0;
  impl_->context->skip_frame = AVDISCARD_NONREF;
  const int result = avcodec_open2(impl_->context, codec, nullptr);
  if (result < 0) {
    throw AppError("could not open H.264 decoder: " + ffmpeg_error(result));
  }
}

H264Decoder::~H264Decoder() = default;

bool H264Decoder::decode(FrameView src, RgbaFrame& dst) {
  if (src.bytes.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
    throw AppError("H.264 packet is too large");
  }
  av_packet_unref(impl_->packet);
  const int packet_size = static_cast<int>(src.bytes.size());
  int result = av_new_packet(impl_->packet, packet_size);
  if (result < 0) {
    throw AppError("could not allocate H.264 packet: " + ffmpeg_error(result));
  }
  std::memcpy(impl_->packet->data, src.bytes.data(), src.bytes.size());
  impl_->packet->pts = static_cast<int64_t>(src.sequence);

  result = avcodec_send_packet(impl_->context, impl_->packet);
  if (result < 0 && result != AVERROR(EAGAIN)) {
    throw AppError("H.264 packet decode failed: " + ffmpeg_error(result));
  }

  bool produced = false;
  while ((result = avcodec_receive_frame(impl_->context, impl_->frame)) >= 0) {
    produced = true;
    dst.size = {static_cast<uint32_t>(impl_->frame->width), static_cast<uint32_t>(impl_->frame->height)};
    dst.sequence = src.sequence;
    dst.pixels.resize(static_cast<size_t>(impl_->frame->width) * static_cast<size_t>(impl_->frame->height) * 4U);
    impl_->scaler = sws_getCachedContext(
        impl_->scaler, impl_->frame->width, impl_->frame->height, static_cast<AVPixelFormat>(impl_->frame->format),
        impl_->frame->width, impl_->frame->height, AV_PIX_FMT_RGBA, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    if (impl_->scaler == nullptr) {
      throw AppError("could not create H.264 color converter");
    }
    uint8_t* output[] = {reinterpret_cast<uint8_t*>(dst.pixels.data())};
    const int strides[] = {impl_->frame->width * 4};
    sws_scale(impl_->scaler, impl_->frame->data, impl_->frame->linesize, 0, impl_->frame->height, output, strides);
    av_frame_unref(impl_->frame);
  }
  if (result != AVERROR(EAGAIN) && result != AVERROR_EOF) {
    throw AppError("H.264 frame decode failed: " + ffmpeg_error(result));
  }
  return produced;
}

} // namespace cv
