// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "avcodec_context.h"

#include <map>
#include <string>

#include <lib/media/codec_impl/codec_buffer.h>

namespace {

// TODO(turnage): Add VP9, and more.
static const std::map<std::string, AVCodecID> codec_ids = {
    {"video/h264", AV_CODEC_ID_H264}};

static inline constexpr uint32_t make_fourcc(uint8_t a, uint8_t b, uint8_t c,
                                             uint8_t d) {
  return (static_cast<uint32_t>(d) << 24) | (static_cast<uint32_t>(c) << 16) |
         (static_cast<uint32_t>(b) << 8) | static_cast<uint32_t>(a);
}

}  // namespace

std::optional<std::unique_ptr<AvCodecContext>> AvCodecContext::CreateDecoder(
    const fuchsia::media::FormatDetails& format_details,
    GetBufferCallback get_buffer_callback) {
  avcodec_register_all();
  if (!format_details.has_mime_type()) {
    return std::nullopt;
  }
  auto codec_id = codec_ids.find(format_details.mime_type());
  if (codec_id == codec_ids.end()) {
    return std::nullopt;
  }

  AVCodec* codec = avcodec_find_decoder(codec_id->second);
  ZX_DEBUG_ASSERT(codec);
  ZX_DEBUG_ASSERT(av_codec_is_decoder(codec));
  auto avcodec_context =
      std::unique_ptr<AVCodecContext, fit::function<void(AVCodecContext*)>>(
          avcodec_alloc_context3(codec), [](AVCodecContext* avcodec_context) {
            avcodec_free_context(&avcodec_context);
          });
  ZX_ASSERT(avcodec_context);

  // This flag must be set in case our packets come on NAL boundaries
  // and not just frame boundaries.
  avcodec_context->flags2 |= AV_CODEC_FLAG2_CHUNKS;

  // This flag is required to override get_buffer2.
  ZX_ASSERT(avcodec_context->codec->capabilities & AV_CODEC_CAP_DR1);

  avcodec_context->get_buffer2 = AvCodecContext::GetBufferCallbackRouter;

  std::unique_ptr<AvCodecContext> decoder(new AvCodecContext(
      std::move(avcodec_context), std::move(get_buffer_callback)));

  if (format_details.has_oob_bytes() && !format_details.oob_bytes().empty()) {
    // Freed in AVCodecContext deleter in avcodec_free.
    const std::vector<uint8_t>& oob = format_details.oob_bytes();
    auto* extradata = reinterpret_cast<uint8_t*>(av_malloc(oob.size()));
    ZX_ASSERT(extradata);
    std::memcpy(extradata, oob.data(), oob.size());
    decoder->avcodec_context_->extradata = extradata;
    decoder->avcodec_context_->extradata_size = oob.size();
  }

  int open_error =
      avcodec_open2(decoder->avcodec_context_.get(), codec, nullptr);
  ZX_ASSERT(!open_error);
  ZX_DEBUG_ASSERT(avcodec_is_open(decoder->avcodec_context_.get()));

  return decoder;
}

int AvCodecContext::SendPacket(const CodecPacket* codec_packet) {
  ZX_DEBUG_ASSERT(codec_packet);
  ZX_DEBUG_ASSERT(avcodec_context_);
  ZX_DEBUG_ASSERT(avcodec_is_open(avcodec_context_.get()));
  ZX_DEBUG_ASSERT(av_codec_is_decoder(avcodec_context_->codec));
  ZX_DEBUG_ASSERT(codec_packet->has_start_offset());
  ZX_DEBUG_ASSERT(codec_packet->has_valid_length_bytes());
  ZX_DEBUG_ASSERT(codec_packet->buffer());

  AVPacket packet;
  av_init_packet(&packet);
  packet.data =
      codec_packet->buffer()->buffer_base() + codec_packet->start_offset();
  packet.size = codec_packet->valid_length_bytes();

  if (codec_packet->has_timestamp_ish()) {
    packet.pts = codec_packet->timestamp_ish();
  }

  return avcodec_send_packet(avcodec_context_.get(), &packet);
}

std::pair<int, AvCodecContext::AVFramePtr> AvCodecContext::ReceiveFrame() {
  ZX_DEBUG_ASSERT(avcodec_context_);
  ZX_DEBUG_ASSERT(avcodec_is_open(avcodec_context_.get()));
  ZX_DEBUG_ASSERT(av_codec_is_decoder(avcodec_context_->codec));

  AVFramePtr frame(av_frame_alloc(),
                   [](AVFrame* frame) { av_frame_free(&frame); });
  // If we can't allocate a frame, abort this isolate process.
  ZX_ASSERT(frame);

  int result_code = avcodec_receive_frame(avcodec_context_.get(), frame.get());
  if (result_code < 0) {
    return {result_code, nullptr};
  }

  return {result_code, std::move(frame)};
}

int AvCodecContext::EndStream() {
  ZX_DEBUG_ASSERT(avcodec_context_);
  ZX_DEBUG_ASSERT(avcodec_is_open(avcodec_context_.get()));
  ZX_DEBUG_ASSERT(av_codec_is_decoder(avcodec_context_->codec));
  return avcodec_send_packet(avcodec_context_.get(), nullptr);
}

AvCodecContext::FrameBufferRequest AvCodecContext::frame_buffer_request(
    AVFrame* frame) const {
  ZX_DEBUG_ASSERT(avcodec_context_);
  ZX_DEBUG_ASSERT(avcodec_is_open(avcodec_context_.get()));
  ZX_DEBUG_ASSERT(av_codec_is_decoder(avcodec_context_->codec));
  // TODO(turnage): Accept 10 bit YUV formats.
  ZX_DEBUG_ASSERT(frame->format == AV_PIX_FMT_YUV420P);
  // We only implement right and bottom crops, not left or top crops.
  ZX_ASSERT(frame->crop_left == 0);
  ZX_ASSERT(frame->crop_top == 0);

  int linesizes[4];
  av_image_fill_linesizes(linesizes, static_cast<AVPixelFormat>(frame->format),
                          frame->width);

  fuchsia::media::VideoUncompressedFormat uncompressed_format;
  uncompressed_format.fourcc = make_fourcc('Y', 'V', '1', '2');

  uncompressed_format.primary_start_offset = 0;
  uncompressed_format.primary_pixel_stride = 1;
  uncompressed_format.primary_line_stride_bytes = linesizes[0];
  uncompressed_format.primary_width_pixels = frame->width;
  uncompressed_format.primary_height_pixels = frame->height;
  uncompressed_format.primary_display_width_pixels =
      frame->width - frame->crop_right;
  uncompressed_format.primary_display_height_pixels =
      frame->height - frame->crop_bottom;

  // TODO(dustingreen): remove this field from the VideoUncompressedFormat or
  // specify separately for primary / secondary.
  uncompressed_format.planar = true;
  uncompressed_format.swizzled = false;

  uncompressed_format.secondary_pixel_stride = 1;
  uncompressed_format.secondary_width_pixels = frame->width / 2;
  uncompressed_format.secondary_height_pixels = frame->height / 2;
  uncompressed_format.secondary_line_stride_bytes = linesizes[1];
  uncompressed_format.secondary_start_offset = linesizes[0] * frame->height;

  uncompressed_format.tertiary_start_offset =
      uncompressed_format.secondary_start_offset +
      uncompressed_format.secondary_height_pixels * linesizes[1];

  uncompressed_format.has_pixel_aspect_ratio = !!frame->sample_aspect_ratio.num;
  if (uncompressed_format.has_pixel_aspect_ratio) {
    uncompressed_format.pixel_aspect_ratio_width =
        frame->sample_aspect_ratio.num;
    uncompressed_format.pixel_aspect_ratio_height =
        frame->sample_aspect_ratio.den;
  }

  size_t buffer_bytes_needed = av_image_get_buffer_size(
      static_cast<AVPixelFormat>(frame->format), frame->width, frame->height,
      /*align=*/1);

  return {.format = std::move(uncompressed_format),
          .buffer_bytes_needed = buffer_bytes_needed};
}

// static
int AvCodecContext::GetBufferCallbackRouter(AVCodecContext* avcodec_context,
                                            AVFrame* frame, int flags) {
  auto instance = reinterpret_cast<AvCodecContext*>(avcodec_context->opaque);
  ZX_DEBUG_ASSERT(instance);
  return instance->GetBufferHandler(avcodec_context, frame, flags);
}

int AvCodecContext::GetBufferHandler(AVCodecContext* avcodec_context,
                                     AVFrame* frame, int flags) {
  ZX_DEBUG_ASSERT(avcodec_context_);
  ZX_DEBUG_ASSERT(get_buffer_callback_);
  ZX_DEBUG_ASSERT(frame->width);

  return get_buffer_callback_(frame_buffer_request(frame), avcodec_context,
                              frame, flags);
}

AvCodecContext::AvCodecContext(
    std::unique_ptr<AVCodecContext, fit::function<void(AVCodecContext*)>>
        avcodec_context,
    GetBufferCallback get_buffer_callback)
    : avcodec_context_(std::move(avcodec_context)),
      get_buffer_callback_(std::move(get_buffer_callback)) {
  ZX_DEBUG_ASSERT(avcodec_context_);
  ZX_DEBUG_ASSERT(get_buffer_callback_);

  avcodec_context_->opaque = this;
}
