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

namespace {

// A client using the min shouldn't necessarily expect performance to be
// acceptable when running higher bit-rates.
constexpr uint32_t kInputPerPacketBufferBytesMin = 8 * 1024;
// This is an arbitrary cap for now.
constexpr uint32_t kInputPerPacketBufferBytesMax = 4 * 1024 * 1024;

// Arbitrary limit; specific value is historical.
static constexpr uint32_t kMaxOutputBufferCount = 34;
// Arbitrary limit.
static constexpr uint32_t kMaxInputBufferCount = 256;

}  // namespace

CodecAdapterFfmpegDecoder::CodecAdapterFfmpegDecoder(std::mutex& lock,
                                                     CodecAdapterEvents* codec_adapter_events)
    : CodecAdapterSW(lock, codec_adapter_events) {}

CodecAdapterFfmpegDecoder::~CodecAdapterFfmpegDecoder() = default;

void CodecAdapterFfmpegDecoder::ProcessInputLoop() {
  std::optional<CodecInputItem> maybe_input_item;
  while ((maybe_input_item = input_queue_.WaitForElement())) {
    CodecInputItem input_item = std::move(maybe_input_item.value());
    if (input_item.is_format_details()) {
      if (avcodec_context_) {
        events_->onCoreCodecFailCodec("Midstream input format change is not supported.");
        return;
      }
      auto maybe_avcodec_context = AvCodecContext::CreateDecoder(
          input_item.format_details(),
          [this](const AvCodecContext::FrameBufferRequest& frame_buffer_request,
                 AVCodecContext* avcodec_context, AVFrame* frame, int flags) {
            return GetBuffer(frame_buffer_request, avcodec_context, frame, flags);
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
        events_->onCoreCodecFailCodec("Failed to decode input packet with ffmpeg error: %s",
                                      av_err2str(result));
        return;
      }

      events_->onCoreCodecInputPacketDone(input_item.packet());

      DecodeFrames();
    }
  }
}

void CodecAdapterFfmpegDecoder::CleanUpAfterStream() { avcodec_context_ = nullptr; }

std::pair<fuchsia::media::FormatDetails, size_t> CodecAdapterFfmpegDecoder::OutputFormatDetails() {
  std::lock_guard<std::mutex> lock(lock_);
  ZX_ASSERT(decoded_output_info_.has_value());

  auto& [uncompressed_format, per_packet_buffer_bytes] = decoded_output_info_.value();

  fuchsia::media::FormatDetails format_details;

  format_details.set_mime_type("video/raw");

  fuchsia::media::VideoFormat video_format;
  video_format.set_uncompressed(fidl::Clone(uncompressed_format));

  format_details.mutable_domain()->set_video(std::move(video_format));

  return {std::move(format_details), per_packet_buffer_bytes};
}

void CodecAdapterFfmpegDecoder::FfmpegFreeBufferCallback(void* ctx, uint8_t* base) {
  auto* self = reinterpret_cast<CodecAdapterFfmpegDecoder*>(ctx);
  self->output_buffer_pool_.FreeBuffer(base);
}

int CodecAdapterFfmpegDecoder::GetBuffer(
    const AvCodecContext::FrameBufferRequest& decoded_output_info, AVCodecContext* avcodec_context,
    AVFrame* frame, int flags) {
  size_t buffer_size;
  bool should_config_output = false;
  bool output_increased_in_size = false;
  bool need_new_buffers = false;
  {
    std::lock_guard<std::mutex> lock(lock_);
    need_new_buffers = !decoded_output_info_;
    if (!decoded_output_info_ ||
        !fidl::Equals((*decoded_output_info_).format, decoded_output_info.format)) {
      output_increased_in_size =
          decoded_output_info_.has_value() &&
          decoded_output_info.buffer_bytes_needed > (*decoded_output_info_).buffer_bytes_needed;
      decoded_output_info_ = {.format = fidl::Clone(decoded_output_info.format),
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
    events_->onCoreCodecMidStreamOutputConstraintsChange(
        /*output_re_config_required=*/need_new_buffers);
  }

  auto buffer = output_buffer_pool_.AllocateBuffer(decoded_output_info.buffer_bytes_needed);
  if (!buffer) {
    // This stream is stopping. We let ffmpeg allocate just so it can exit
    // cleanly.
    return avcodec_default_get_buffer2(avcodec_context, frame, flags);
  }

  AVPixelFormat pix_fmt = FourccToPixelFormat(decoded_output_info.format.fourcc);
  if (pix_fmt == AV_PIX_FMT_NONE) {
    events_->onCoreCodecFailCodec("Unsupported format: %d", pix_fmt);
    return -1;
  }

  AVBufferRef* buffer_ref = av_buffer_create(buffer->base(), static_cast<int>(buffer->size()),
                                             FfmpegFreeBufferCallback, this, flags);

  int fill_arrays_status =
      av_image_fill_arrays(frame->data, frame->linesize, buffer_ref->data, pix_fmt,
                           decoded_output_info.format.primary_width_pixels,
                           decoded_output_info.format.primary_height_pixels, 1);
  if (fill_arrays_status < 0) {
    events_->onCoreCodecFailCodec("Ffmpeg fill arrays failed: %d", fill_arrays_status);
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
      events_->onCoreCodecFailCodec("DecodeFrames(): Failed to decode frame: %s",
                                    av_err2str(error));
      return;
    }

    std::optional<CodecPacket*> maybe_output_packet = free_output_packets_.WaitForElement();
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
      ZX_DEBUG_ASSERT(in_use_by_client_.find(output_packet) == in_use_by_client_.end());
      in_use_by_client_.emplace(output_packet, std::move(frame));
    }

    events_->onCoreCodecOutputPacket(output_packet,
                                     /*error_detected_before=*/false,
                                     /*error_detected_during=*/false);
  }
}

fuchsia::sysmem::BufferCollectionConstraints
CodecAdapterFfmpegDecoder::CoreCodecGetBufferCollectionConstraints(
    CodecPort port, const fuchsia::media::StreamBufferConstraints& stream_buffer_constraints,
    const fuchsia::media::StreamBufferPartialSettings& partial_settings) {
  std::lock_guard<std::mutex> lock(lock_);

  fuchsia::sysmem::BufferCollectionConstraints result;

  // We reported single_buffer_mode_allowed false (or un-set), and CodecImpl
  // will have failed the codec already by this point if the client tried to
  // use single_buffer_mode true.
  ZX_DEBUG_ASSERT(!partial_settings.has_single_buffer_mode() ||
                  !partial_settings.single_buffer_mode());
  // The CodecImpl won't hand us the sysmem token, so we shouldn't expect to
  // have the token here.
  ZX_DEBUG_ASSERT(!partial_settings.has_sysmem_token());

  // TODO(fxbug.dev/13531): plumb/permit range of buffer count from further down,
  // instead of single number frame_count, and set this to the actual
  // stream-required # of reference frames + # that can concurrently decode.
  // Packets and buffers are not the same thing, and we should permit the # of
  // packets to be >= the # of buffers.  We shouldn't be
  // allocating buffers on behalf of the client here, but until we plumb the
  // range of frame_count and are more flexible on # of allocated buffers, we
  // have to make sure there are at least as many buffers as packets.  We
  // categorize the buffers as for camping and for slack.  This should change to
  // be just the buffers needed for camping and maybe 1 for shared slack.  If
  // the client wants more buffers the client can demand buffers in its own
  // fuchsia::sysmem::BufferCollection::SetConstraints().
  if (port == kOutputPort) {
    result.min_buffer_count_for_camping = kMinOutputBufferCountForCamping;
  } else {
    result.min_buffer_count_for_camping = kMinInputBufferCountForCamping;
  }

  ZX_DEBUG_ASSERT(result.min_buffer_count_for_dedicated_slack == 0);
  ZX_DEBUG_ASSERT(result.min_buffer_count_for_shared_slack == 0);

  if (port == kOutputPort) {
    result.max_buffer_count = kMaxOutputBufferCount;
  } else {
    result.max_buffer_count = kMaxInputBufferCount;
  }

  uint32_t per_packet_buffer_bytes_min;
  uint32_t per_packet_buffer_bytes_max;
  if (port == kInputPort) {
    per_packet_buffer_bytes_min = kInputPerPacketBufferBytesMin;
    per_packet_buffer_bytes_max = kInputPerPacketBufferBytesMax;
  } else {
    ZX_ASSERT(decoded_output_info_.has_value());
    auto& [uncompressed_format, per_packet_buffer_bytes] = decoded_output_info_.value();

    ZX_DEBUG_ASSERT(port == kOutputPort);
    // NV12, based on min stride.
    per_packet_buffer_bytes_min = uncompressed_format.primary_line_stride_bytes *
                                  uncompressed_format.primary_height_pixels * 3 / 2;
    // At least for now, don't cap the per-packet buffer size for output.  The
    // HW only cares about the portion we set up for output anyway, and the
    // client has no way to force output to occur into portions of the output
    // buffer beyond what's implied by the max supported image dimensions.
    per_packet_buffer_bytes_max = 0xFFFFFFFF;
  }

  result.has_buffer_memory_constraints = true;
  result.buffer_memory_constraints.min_size_bytes = per_packet_buffer_bytes_min;
  result.buffer_memory_constraints.max_size_bytes = per_packet_buffer_bytes_max;

  // These are all false because SW decode.
  result.buffer_memory_constraints.physically_contiguous_required = false;
  result.buffer_memory_constraints.secure_required = false;

  if (port == kOutputPort) {
    ZX_ASSERT(decoded_output_info_.has_value());
    auto& [uncompressed_format, per_packet_buffer_bytes] = decoded_output_info_.value();

    result.image_format_constraints_count = 1;
    fuchsia::sysmem::ImageFormatConstraints& image_constraints = result.image_format_constraints[0];
    image_constraints.pixel_format.type = fuchsia::sysmem::PixelFormatType::YV12;
    // TODO(fxbug.dev/13532): confirm that REC709 is always what we want here, or plumb
    // actual YUV color space if it can ever be REC601_*.  Since 2020 and 2100
    // are minimum 10 bits per Y sample and we're outputting NV12, 601 is the
    // only other potential possibility here.
    image_constraints.color_spaces_count = 1;
    image_constraints.color_space[0].type = fuchsia::sysmem::ColorSpaceType::REC709;

    // The non-"required_" fields indicate the decoder's ability to potentially
    // output frames at various dimensions as coded in the stream.  Aside from
    // the current stream being somewhere in these bounds, these have nothing to
    // do with the current stream in particular.
    image_constraints.min_coded_width = 16;
    image_constraints.max_coded_width = 3840;
    image_constraints.min_coded_height = 16;
    // This intentionally isn't the height of a 4k frame.  See
    // max_coded_width_times_coded_height.  We intentionally constrain the max
    // dimension in width or height to the width of a 4k frame.  While the HW
    // might be able to go bigger than that as long as the other dimension is
    // smaller to compensate, we don't really need to enable any larger than
    // 4k's width in either dimension, so we don't.
    image_constraints.max_coded_height = 3840;
    image_constraints.min_bytes_per_row = 16;
    // no hard-coded max stride, at least for now
    image_constraints.max_bytes_per_row = 0xFFFFFFFF;
    image_constraints.max_coded_width_times_coded_height = 3840 * 2160;
    image_constraints.layers = 1;
    image_constraints.coded_width_divisor = 16;
    image_constraints.coded_height_divisor = 16;
    image_constraints.bytes_per_row_divisor = 16;
    // TODO(dustingreen): Since this is a producer that will always produce at
    // offset 0 of a physical page, we don't really care if this field is
    // consistent with any constraints re. what the HW can do.
    image_constraints.start_offset_divisor = 1;
    // Odd display dimensions are permitted, but these don't imply odd YV12
    // dimensions - those are constrainted by coded_width_divisor and
    // coded_height_divisor which are both 16.
    image_constraints.display_width_divisor = 1;
    image_constraints.display_height_divisor = 1;

    // The decoder is producing frames and the decoder has no choice but to
    // produce frames at their coded size.  The decoder wants to potentially be
    // able to support a stream with dynamic resolution, potentially including
    // dimensions both less than and greater than the dimensions that led to the
    // current need to allocate a BufferCollection.  For this reason, the
    // required_ fields are set to the exact current dimensions, and the
    // permitted (non-required_) fields is set to the full potential range that
    // the decoder could potentially output.  If an initiator wants to require a
    // larger range of dimensions that includes the required range indicated
    // here (via a-priori knowledge of the potential stream dimensions), an
    // initiator is free to do so.
    image_constraints.required_min_coded_width = uncompressed_format.primary_width_pixels;
    image_constraints.required_max_coded_width = uncompressed_format.primary_width_pixels;
    image_constraints.required_min_coded_height = uncompressed_format.primary_height_pixels;
    image_constraints.required_max_coded_height = uncompressed_format.primary_height_pixels;
    // As needed we might want to plumb more flexibility for the stride.
    image_constraints.required_min_bytes_per_row = uncompressed_format.primary_line_stride_bytes;
    image_constraints.required_max_bytes_per_row = uncompressed_format.primary_line_stride_bytes;
  } else {
    ZX_DEBUG_ASSERT(result.image_format_constraints_count == 0);
  }

  // We don't have to fill out usage - CodecImpl takes care of that.
  ZX_DEBUG_ASSERT(!result.usage.cpu);
  ZX_DEBUG_ASSERT(!result.usage.display);
  ZX_DEBUG_ASSERT(!result.usage.vulkan);
  ZX_DEBUG_ASSERT(!result.usage.video);

  return result;
}

void CodecAdapterFfmpegDecoder::CoreCodecSetBufferCollectionInfo(
    CodecPort port, const fuchsia::sysmem::BufferCollectionInfo_2& buffer_collection_info) {
  if (port == kInputPort) {
    ZX_DEBUG_ASSERT(buffer_collection_info.buffer_count >= kMinInputBufferCountForCamping);
  } else {
    ZX_DEBUG_ASSERT(buffer_collection_info.buffer_count >= kMinOutputBufferCountForCamping);
  }
}
