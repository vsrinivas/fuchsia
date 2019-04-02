// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_adapter_ffmpeg_decoder.h"

extern "C" {
#include "libavutil/imgutils.h"
}

#include <lib/async/cpp/task.h>
#include <lib/fit/defer.h>
#include <lib/media/codec_impl/codec_buffer.h>
#include <lib/media/codec_impl/fourcc.h>

namespace {

AVPixelFormat FourccToPixelFormat(uint32_t fourcc) {
  switch (fourcc) {
    case make_fourcc('Y', 'V', '1', '2'):
      return AV_PIX_FMT_YUV420P;
    default:
      return AV_PIX_FMT_NONE;
  }
}

}  // namespace

CodecAdapterFfmpegDecoder::CodecAdapterFfmpegDecoder(
    std::mutex& lock, CodecAdapterEvents* codec_adapter_events)
    : CodecAdapterSW(lock, codec_adapter_events) {}

CodecAdapterFfmpegDecoder::~CodecAdapterFfmpegDecoder() = default;

void CodecAdapterFfmpegDecoder::ProcessInputLoop() {
  std::optional<CodecInputItem> maybe_input_item;
  while ((maybe_input_item = input_queue_.WaitForElement())) {
    CodecInputItem input_item = std::move(maybe_input_item.value());
    if (input_item.is_format_details()) {
      if (avcodec_context_) {
        events_->onCoreCodecFailCodec(
            "Midstream input  format change is not supported.");
        return;
      }
      auto maybe_avcodec_context = AvCodecContext::CreateDecoder(
          input_item.format_details(),
          [this](const AvCodecContext::FrameBufferRequest& frame_buffer_request,
                 AVCodecContext* avcodec_context, AVFrame* frame, int flags) {
            return GetBuffer(frame_buffer_request, avcodec_context, frame,
                             flags);
          });
      if (!maybe_avcodec_context) {
        events_->onCoreCodecFailCodec("Failed to create ffmpeg decoder.");
        return;
      }
      avcodec_context_ = std::move(maybe_avcodec_context.value());
    } else if (input_item.is_end_of_stream()) {
      ZX_ASSERT(avcodec_context_);
      avcodec_context_->EndStream();
      DecodeFrames();
    } else if (input_item.is_packet()) {
      ZX_DEBUG_ASSERT(avcodec_context_);
      int result = avcodec_context_->SendPacket(input_item.packet());
      if (result < 0) {
        events_->onCoreCodecFailCodec(
            "Failed to decode input packet with ffmpeg error: %s",
            av_err2str(result));
        return;
      }

      events_->onCoreCodecInputPacketDone(input_item.packet());

      DecodeFrames();
    }
  }
}

void CodecAdapterFfmpegDecoder::CleanUpAfterStream() {
  avcodec_context_ = nullptr;
}

std::pair<fuchsia::media::FormatDetails, size_t>
CodecAdapterFfmpegDecoder::OutputFormatDetails() {
  std::lock_guard<std::mutex> lock(lock_);
  ZX_ASSERT(decoded_output_info_.has_value());

  auto& [uncompressed_format, per_packet_buffer_bytes] =
      decoded_output_info_.value();

  fuchsia::media::FormatDetails format_details;

  format_details.set_mime_type("video/raw");

  fuchsia::media::VideoFormat video_format;
  video_format.set_uncompressed(fidl::Clone(uncompressed_format));

  format_details.mutable_domain()->set_video(std::move(video_format));

  return {std::move(format_details), per_packet_buffer_bytes};
}

void CodecAdapterFfmpegDecoder::FfmpegFreeBufferCallback(void* ctx,
                                                         uint8_t* base) {
  auto* self = reinterpret_cast<CodecAdapterFfmpegDecoder*>(ctx);
  self->output_buffer_pool_.FreeBuffer(base);
}

int CodecAdapterFfmpegDecoder::GetBuffer(
    const AvCodecContext::FrameBufferRequest& decoded_output_info,
    AVCodecContext* avcodec_context, AVFrame* frame, int flags) {
  size_t buffer_size;
  bool should_config_output = false;
  bool output_increased_in_size = false;
  bool need_new_buffers = false;
  {
    std::lock_guard<std::mutex> lock(lock_);
    need_new_buffers = !decoded_output_info_;
    if (!decoded_output_info_ ||
        (*decoded_output_info_).format != decoded_output_info.format) {
      output_increased_in_size =
          decoded_output_info_.has_value() &&
          decoded_output_info.buffer_bytes_needed >
              (*decoded_output_info_).buffer_bytes_needed;
      decoded_output_info_ = {
          .format = fidl::Clone(decoded_output_info.format),
          .buffer_bytes_needed = decoded_output_info.buffer_bytes_needed};
      buffer_size = (*decoded_output_info_).buffer_bytes_needed;
      should_config_output = true;
    }
  }

  if (output_increased_in_size) {
    events_->onCoreCodecFailCodec(
        "Midstream output config change to larger format is not supported.");
    return avcodec_default_get_buffer2(avcodec_context, frame, flags);
  }

  if (should_config_output) {
    events_->onCoreCodecMidStreamOutputConfigChange(
        /*output_re_config_required=*/need_new_buffers);
  }

  auto buffer = output_buffer_pool_.AllocateBuffer(
      decoded_output_info.buffer_bytes_needed);
  if (!buffer) {
    // This stream is stopping. We let ffmpeg allocate just so it can exit
    // cleanly.
    return avcodec_default_get_buffer2(avcodec_context, frame, flags);
  }

  AVPixelFormat pix_fmt =
      FourccToPixelFormat(decoded_output_info.format.fourcc);
  if (pix_fmt == AV_PIX_FMT_NONE) {
    events_->onCoreCodecFailCodec("Unsupported format: %d", pix_fmt);
    return -1;
  }

  AVBufferRef* buffer_ref = av_buffer_create(
      (*buffer)->buffer_base(), static_cast<int>((*buffer)->buffer_size()),
      FfmpegFreeBufferCallback, this, flags);

  int fill_arrays_status = av_image_fill_arrays(
      frame->data, frame->linesize, buffer_ref->data, pix_fmt,
      decoded_output_info.format.primary_width_pixels,
      decoded_output_info.format.primary_height_pixels, 1);
  if (fill_arrays_status < 0) {
    events_->onCoreCodecFailCodec("Ffmpeg fill arrays failed: %d",
                                  fill_arrays_status);
    return -1;
  }

  // IYUV is not YV12. Ffmpeg only decodes into IYUV. The difference between
  // YV12 and IYUV is the order of the U and V planes. Here we trick Ffmpeg
  // into writing them in YV12 order relative to one another.
  std::swap(frame->data[1], frame->data[2]);

  frame->buf[0] = buffer_ref;
  // ffmpeg says to set extended_data to data if we're not using extended_data
  frame->extended_data = frame->data;

  return 0;
}

void CodecAdapterFfmpegDecoder::DecodeFrames() {
  ZX_DEBUG_ASSERT(thrd_current() == input_processing_thread_);
  ZX_DEBUG_ASSERT(avcodec_context_);

  while (true) {
    auto [error, frame] = avcodec_context_->ReceiveFrame();
    if (error == AVERROR(EAGAIN)) {
      return;
    } else if (error == AVERROR_EOF) {
      events_->onCoreCodecOutputEndOfStream(/*error_detected_before=*/false);
      return;
    } else if (error < 0) {
      events_->onCoreCodecFailCodec(
          "DecodeFrames(): Failed to decode frame: %s", av_err2str(error));
      return;
    }

    std::optional<CodecPacket*> maybe_output_packet =
        free_output_packets_.WaitForElement();
    if (!maybe_output_packet) {
      return;
    }
    auto output_packet = *maybe_output_packet;

    auto buffer_alloc = output_buffer_pool_.FindBufferByBase(frame->data[0]);
    ZX_ASSERT(buffer_alloc);

    output_packet->SetBuffer(buffer_alloc->buffer);
    output_packet->SetStartOffset(0);
    output_packet->SetValidLengthBytes(buffer_alloc->bytes_used);
    output_packet->SetTimstampIsh(frame->pts);

    {
      std::lock_guard<std::mutex> lock(lock_);
      ZX_DEBUG_ASSERT(in_use_by_client_.find(output_packet) ==
                      in_use_by_client_.end());
      in_use_by_client_.emplace(output_packet, std::move(frame));
    }

    events_->onCoreCodecOutputPacket(output_packet,
                                     /*error_detected_before=*/false,
                                     /*error_detected_during=*/false);
  }
}