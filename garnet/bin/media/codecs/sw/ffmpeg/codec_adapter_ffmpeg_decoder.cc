// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_adapter_ffmpeg_decoder.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/defer.h>
#include <lib/media/codec_impl/codec_buffer.h>

CodecAdapterFfmpegDecoder::CodecAdapterFfmpegDecoder(
    std::mutex& lock, CodecAdapterEvents* codec_adapter_events)
    : CodecAdapterSW(lock, codec_adapter_events) {}

CodecAdapterFfmpegDecoder::~CodecAdapterFfmpegDecoder() = default;

void CodecAdapterFfmpegDecoder::CoreCodecAddBuffer(CodecPort port,
                                                   const CodecBuffer* buffer) {
  if (port != kOutputPort) {
    return;
  }
  output_buffer_pool_.AddBuffer(buffer);
}

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
          [this](const BufferPool::FrameBufferRequest& frame_buffer_request,
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

void CodecAdapterFfmpegDecoder::UnreferenceOutputPacket(CodecPacket* packet) {
  if (packet->buffer()) {
    AvCodecContext::AVFramePtr frame;
    {
      std::lock_guard<std::mutex> lock(lock_);
      frame = std::move(in_use_by_client_[packet]);
      in_use_by_client_.erase(packet);
    }

    // ~ frame, which may trigger our buffer free callback.
  }
}

void CodecAdapterFfmpegDecoder::UnreferenceClientBuffers() {
  output_buffer_pool_.Reset();

  std::map<CodecPacket*, AvCodecContext::AVFramePtr> to_drop;
  {
    std::lock_guard<std::mutex> lock(lock_);
    std::swap(to_drop, in_use_by_client_);
  }
  // ~ to_drop

  // Given that we currently fail the codec on mid-stream output format
  // change (elsewhere), the decoder won't have frames referenced here.
  ZX_DEBUG_ASSERT(!output_buffer_pool_.has_buffers_in_use());
}

void CodecAdapterFfmpegDecoder::BeginStopInputProcessing() {
  output_buffer_pool_.StopAllWaits();
}

void CodecAdapterFfmpegDecoder::CleanUpAfterStream() {
  output_buffer_pool_.Reset(/*keep_data=*/true);
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

int CodecAdapterFfmpegDecoder::GetBuffer(
    const BufferPool::FrameBufferRequest& decoded_output_info,
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

  BufferPool::Status status = output_buffer_pool_.AttachFrameToBuffer(
      frame, decoded_output_info, flags);
  if (status == BufferPool::SHUTDOWN) {
    // This stream is stopping. We let ffmpeg allocate just so it can exit
    // cleanly.
    return avcodec_default_get_buffer2(avcodec_context, frame, flags);
  } else if (status != BufferPool::OK) {
    events_->onCoreCodecFailCodec(
        "Could not find output buffer; BufferPool::Status: %d", status);
    return -1;
  }

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

    auto buffer_alloc = output_buffer_pool_.FindBufferByFrame(frame.get());
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