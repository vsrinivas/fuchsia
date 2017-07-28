// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/ffmpeg/ffmpeg_decoder_base.h"

#include "apps/media/src/ffmpeg/av_codec_context.h"
#include "apps/tracing/lib/trace/event.h"
#include "lib/ftl/logging.h"

namespace media {

FfmpegDecoderBase::FfmpegDecoderBase(AvCodecContextPtr av_codec_context)
    : av_codec_context_(std::move(av_codec_context)),
      av_frame_ptr_(ffmpeg::AvFrame::Create()) {
  FTL_DCHECK(av_codec_context_);

  av_codec_context_->opaque = this;
  av_codec_context_->get_buffer2 = AllocateBufferForAvFrame;
  av_codec_context_->refcounted_frames = 1;
}

FfmpegDecoderBase::~FfmpegDecoderBase() {}

std::unique_ptr<StreamType> FfmpegDecoderBase::output_stream_type() {
  return AvCodecContext::GetStreamType(*av_codec_context_);
}

void FfmpegDecoderBase::Flush() {
  FTL_DCHECK(av_codec_context_);
  avcodec_flush_buffers(av_codec_context_.get());
  next_pts_ = Packet::kUnknownPts;
}

bool FfmpegDecoderBase::TransformPacket(const PacketPtr& input,
                                        bool new_input,
                                        PayloadAllocator* allocator,
                                        PacketPtr* output) {
  TRACE_DURATION(
      "motown", "DecodePacket", "type",
      (av_codec_context_->codec_type == AVMEDIA_TYPE_VIDEO ? "video"
                                                           : "audio"));
  FTL_DCHECK(input);
  FTL_DCHECK(allocator);
  FTL_DCHECK(output);

  *output = nullptr;

  if (new_input) {
    if (input->size() == 0 && !input->end_of_stream()) {
      // Throw away empty packets that aren't end-of-stream packets. The
      // underlying decoder interprets an empty packet as end-of-stream.
      // Returning true here causes the stage to release the input packet and
      // call again with a new one.
      return true;
    }

    OnNewInputPacket(input);

    AVPacket av_packet;
    av_init_packet(&av_packet);
    av_packet.data = reinterpret_cast<uint8_t*>(input->payload());
    av_packet.size = input->size();
    av_packet.pts = input->pts();

    if (input->keyframe()) {
      av_packet.flags |= AV_PKT_FLAG_KEY;
    }

    // Used during avcodec_send_packet by AllocateBufferForAvFrame.
    allocator_ = allocator;
    int result = avcodec_send_packet(av_codec_context_.get(), &av_packet);
    allocator_ = nullptr;

    if (result != 0) {
      FTL_DLOG(ERROR) << "avcodec_send_packet failed " << result;
      if (input->end_of_stream()) {
        // The input packet was end-of-stream. We won't get called again before
        // a flush, so make sure the output gets an end-of-stream packet.
        *output = Packet::CreateEndOfStream(next_pts_, pts_rate_);
      }

      return true;
    }
  }

  // Used during avcodec_receive_frame by AllocateBufferForAvFrame.
  allocator_ = allocator;
  int result =
      avcodec_receive_frame(av_codec_context_.get(), av_frame_ptr_.get());
  allocator_ = nullptr;

  switch (result) {
    case 0:
      // Succeeded, frame produced.
      FTL_DCHECK(allocator);
      *output = CreateOutputPacket(*av_frame_ptr_, allocator);
      av_frame_unref(av_frame_ptr_.get());
      return false;

    case AVERROR(EAGAIN):
      // Succeeded, no frame produced, need another input packet.
      FTL_DCHECK(!input->end_of_stream());
      if (!input->end_of_stream() || input->size() == 0) {
        return true;
      }

      // The input packet is an end-of-stream packet, but it has payload. The
      // underlying decoder interprets an empty packet as end-of-stream, so
      // we need to send it an empty packet. We do this by reentering
      // |TransformPacket|. This is safe, because we get |AVERROR_EOF|, not
      // |AVERROR(EAGAIN)| when the decoder is drained following an empty
      // input packet.
      return TransformPacket(Packet::CreateEndOfStream(next_pts_, pts_rate_),
                             true, allocator, output);

    case AVERROR_EOF:
      // Succeeded, no frame produced, end-of-stream sequence complete.
      FTL_DCHECK(input->end_of_stream());
      *output = Packet::CreateEndOfStream(next_pts_, pts_rate_);
      return true;

    default:
      FTL_DLOG(ERROR) << "avcodec_receive_frame failed " << result;
      if (input->end_of_stream()) {
        // The input packet was end-of-stream. We won't get called again before
        // a flush, so make sure the output gets an end-of-stream packet.
        *output = Packet::CreateEndOfStream(next_pts_, pts_rate_);
      }

      return true;
  }
}

void FfmpegDecoderBase::OnNewInputPacket(const PacketPtr& packet) {}

// static
int FfmpegDecoderBase::AllocateBufferForAvFrame(
    AVCodecContext* av_codec_context,
    AVFrame* av_frame,
    int flags) {
  // It's important to use av_codec_context here rather than context(),
  // because av_codec_context is different for different threads when we're
  // decoding on multiple threads. Be sure to avoid using self->context() or
  // self->av_codec_context_.

  // CODEC_CAP_DR1 is required in order to do allocation this way.
  FTL_DCHECK(av_codec_context->codec->capabilities & CODEC_CAP_DR1);

  FfmpegDecoderBase* self =
      reinterpret_cast<FfmpegDecoderBase*>(av_codec_context->opaque);
  FTL_DCHECK(self);
  FTL_DCHECK(self->allocator_);

  return self->BuildAVFrame(*av_codec_context, av_frame, self->allocator_);
}

// static
void FfmpegDecoderBase::ReleaseBufferForAvFrame(void* opaque, uint8_t* buffer) {
  FTL_DCHECK(opaque);
  FTL_DCHECK(buffer);
  PayloadAllocator* allocator = reinterpret_cast<PayloadAllocator*>(opaque);
  allocator->ReleasePayloadBuffer(buffer);
}

FfmpegDecoderBase::DecoderPacket::~DecoderPacket() {
  av_buffer_unref(&av_buffer_ref_);
}

}  // namespace media
