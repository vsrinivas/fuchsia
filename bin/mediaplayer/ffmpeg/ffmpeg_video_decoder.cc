// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/ffmpeg/ffmpeg_video_decoder.h"

#include <lib/sync/completion.h>
#include <algorithm>
#include "garnet/bin/mediaplayer/ffmpeg/ffmpeg_formatting.h"
#include "lib/fxl/logging.h"
#include "lib/media/timeline/timeline.h"
#include "lib/media/timeline/timeline_rate.h"
extern "C" {
#include "libavutil/imgutils.h"
}

namespace media_player {
namespace {

constexpr uint32_t kOutputMaxPayloadCount = 6;

}  // namespace

// static
std::shared_ptr<Decoder> FfmpegVideoDecoder::Create(
    AvCodecContextPtr av_codec_context) {
  return std::make_shared<FfmpegVideoDecoder>(std::move(av_codec_context));
}

FfmpegVideoDecoder::FfmpegVideoDecoder(AvCodecContextPtr av_codec_context)
    : FfmpegDecoderBase(std::move(av_codec_context)) {
  FXL_DCHECK(context());

  // Turn on multi-proc decoding by allowing the decoder to use three threads
  // (the calling thread and the two specified here). FF_THREAD_FRAME means
  // that threads are assigned an entire frame.
  // TODO(dalesat): Consider using FF_THREAD_SLICE.
  context()->thread_count = 2;
  context()->thread_type = FF_THREAD_FRAME;

  frame_layout_.Update(*context());
}

FfmpegVideoDecoder::~FfmpegVideoDecoder() {}

void FfmpegVideoDecoder::ConfigureConnectors() {
  // TODO(dalesat): Make sure these numbers are adequate.
  // The demux allocates local memory itself, so we don't have to say much
  // here.
  stage()->ConfigureInputToUseLocalMemory(0,   // max_aggregate_payload_size
                                          2);  // max_payload_count

  if (has_size()) {
    configured_output_buffer_size_ = frame_layout_.buffer_size();
    stage()->ConfigureOutputToUseLocalMemory(0, kOutputMaxPayloadCount,
                                             configured_output_buffer_size_);
  } else {
    stage()->ConfigureOutputDeferred();
  }
}

void FfmpegVideoDecoder::OnNewInputPacket(const PacketPtr& packet) {
  FXL_DCHECK(context());
  FXL_DCHECK(packet->pts() != Packet::kUnknownPts);

  if (pts_rate() == media::TimelineRate::Zero) {
    set_pts_rate(packet->pts_rate());
  } else {
    packet->SetPtsRate(pts_rate());
  }

  // We put the pts here so it can be recovered later in CreateOutputPacket.
  // Ffmpeg deals with the frame ordering issues.
  context()->reordered_opaque = packet->pts();
}

int FfmpegVideoDecoder::BuildAVFrame(const AVCodecContext& av_codec_context,
                                     AVFrame* av_frame) {
  FXL_DCHECK(av_frame);

  if (frame_layout_.Update(av_codec_context)) {
    revised_stream_type_ = AvCodecContext::GetStreamType(av_codec_context);
  }

  VideoStreamType::Extent visible_size(av_codec_context.width,
                                       av_codec_context.height);
  const int result =
      av_image_check_size(visible_size.width(), visible_size.height(), 0, NULL);
  if (result < 0) {
    return result;
  }

  // FFmpeg has specific requirements on the allocation size of the frame.  The
  // following logic replicates FFmpeg's allocation strategy to ensure buffers
  // are not overread / overwritten.  See ff_init_buffer_info() for details.

  // When lowres is non-zero, dimensions should be divided by 2^(lowres), but
  // since we don't use this, just FXL_DCHECK that it's zero.
  FXL_DCHECK(av_codec_context.lowres == 0);
  VideoStreamType::Extent coded_size(
      std::max(visible_size.width(),
               static_cast<uint32_t>(av_codec_context.coded_width)),
      std::max(visible_size.height(),
               static_cast<uint32_t>(av_codec_context.coded_height)));

  // TODO(dalesat): For investigation purposes only...remove one day.
  if (first_frame_) {
    first_frame_ = false;
    colorspace_ = av_codec_context.colorspace;
    coded_size_ = coded_size;
  } else {
    if (av_codec_context.colorspace != colorspace_) {
      FXL_LOG(WARNING) << " colorspace changed to "
                       << av_codec_context.colorspace << "\n";
    }
    if (coded_size.width() != coded_size_.width()) {
      FXL_LOG(WARNING) << " coded_size width changed to " << coded_size.width()
                       << "\n";
    }
    if (coded_size.height() != coded_size_.height()) {
      FXL_LOG(WARNING) << " coded_size height changed to "
                       << coded_size.height() << "\n";
    }
    colorspace_ = av_codec_context.colorspace;
    coded_size_ = coded_size;
  }

  size_t buffer_size = frame_layout_.buffer_size();
  if (has_size() && configured_output_buffer_size_ < buffer_size) {
    configured_output_buffer_size_ = buffer_size;

    // We need to configure the output, but that has to happen on the graph
    // thread. Do that and block until it's done.
    sync_completion completion;
    stage()->PostTask([this, buffer_size, &completion]() {
      stage()->ConfigureOutputToUseLocalMemory(
          0,                       // max_aggregate_payload_size
          kOutputMaxPayloadCount,  // max_payload_count
          buffer_size);            // max_payload_size
      sync_completion_signal(&completion);
    });

    sync_completion_wait(&completion, ZX_TIME_INFINITE);
  }

  fbl::RefPtr<PayloadBuffer> payload_buffer =
      stage()->AllocatePayloadBuffer(frame_layout_.buffer_size());

  if (!payload_buffer) {
    FXL_LOG(ERROR) << "failed to allocate payload buffer of size "
                   << frame_layout_.buffer_size();
    return -1;
  }

  // Check that the allocator has met the common alignment requirements and
  // that those requirements are good enough for the decoder.
  FXL_DCHECK(PayloadBuffer::IsAligned(payload_buffer->data()));
  FXL_DCHECK(PayloadBuffer::kByteAlignment >= kFrameBufferAlign);

  // Decoders require a zeroed buffer.
  std::memset(payload_buffer->data(), 0, frame_layout_.buffer_size());

  FXL_DCHECK(frame_layout_.line_stride().size() ==
             frame_layout_.plane_offset().size());

  for (size_t plane = 0; plane < frame_layout_.plane_offset().size(); ++plane) {
    av_frame->data[plane] = reinterpret_cast<uint8_t*>(payload_buffer->data()) +
                            frame_layout_.plane_offset()[plane];
    av_frame->linesize[plane] = frame_layout_.line_stride()[plane];
  }

  // TODO(dalesat): Do we need to attach colorspace info to the packet?

  av_frame->width = coded_size.width();
  av_frame->height = coded_size.height();
  av_frame->format = av_codec_context.pix_fmt;
  av_frame->reordered_opaque = av_codec_context.reordered_opaque;

  FXL_DCHECK(av_frame->data[0] == payload_buffer->data());
  av_frame->buf[0] = CreateAVBuffer(std::move(payload_buffer));

  return 0;
}

PacketPtr FfmpegVideoDecoder::CreateOutputPacket(
    const AVFrame& av_frame, fbl::RefPtr<PayloadBuffer> payload_buffer) {
  FXL_DCHECK(av_frame.buf[0]);
  FXL_DCHECK(payload_buffer);

  // Recover the pts deposited in Decode.
  set_next_pts(av_frame.reordered_opaque);

  PacketPtr packet = Packet::Create(
      av_frame.reordered_opaque, pts_rate(), av_frame.key_frame, false,
      frame_layout_.buffer_size(), std::move(payload_buffer));

  if (revised_stream_type_) {
    packet->SetRevisedStreamType(std::move(revised_stream_type_));
  }

  return packet;
}

const char* FfmpegVideoDecoder::label() const { return "video_decoder"; }

}  // namespace media_player
