// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_player/ffmpeg/ffmpeg_decoder_base.h"

#include <lib/async/cpp/task.h>
#include <trace/event.h>

#include "garnet/bin/media/media_player/ffmpeg/av_codec_context.h"
#include "garnet/bin/media/media_player/framework/formatting.h"
#include "lib/fxl/logging.h"
#include "lib/media/timeline/timeline.h"

namespace media_player {

FfmpegDecoderBase::FfmpegDecoderBase(AvCodecContextPtr av_codec_context)
    : av_codec_context_(std::move(av_codec_context)),
      av_frame_ptr_(ffmpeg::AvFrame::Create()) {
  FXL_DCHECK(av_codec_context_);

  state_ = State::kIdle;

  av_codec_context_->opaque = this;
  av_codec_context_->get_buffer2 = AllocateBufferForAvFrame;
  av_codec_context_->refcounted_frames = 1;

  worker_loop_.StartThread();
}

FfmpegDecoderBase::~FfmpegDecoderBase() {}

std::unique_ptr<StreamType> FfmpegDecoderBase::output_stream_type() const {
  return AvCodecContext::GetStreamType(*av_codec_context_);
}

void FfmpegDecoderBase::GetConfiguration(size_t* input_count,
                                         size_t* output_count) {
  FXL_DCHECK(input_count);
  FXL_DCHECK(output_count);
  *input_count = 1;
  *output_count = 1;
}

void FfmpegDecoderBase::FlushInput(bool hold_frame, size_t input_index,
                                   fxl::Closure callback) {
  FXL_DCHECK(input_index == 0);
  FXL_DCHECK(callback);

  flushing_ = true;

  callback();
}

void FfmpegDecoderBase::FlushOutput(size_t output_index,
                                    fxl::Closure callback) {
  FXL_DCHECK(output_index == 0);
  FXL_DCHECK(callback);

  flushing_ = true;

  async::PostTask(worker_loop_.async(), [this, callback] {
    FXL_DCHECK(av_codec_context_);
    avcodec_flush_buffers(av_codec_context_.get());
    next_pts_ = Packet::kUnknownPts;
    state_ = State::kIdle;
    callback();
  });
}

std::shared_ptr<PayloadAllocator> FfmpegDecoderBase::allocator_for_input(
    size_t input_index) {
  FXL_DCHECK(input_index == 0);
  return nullptr;
}

void FfmpegDecoderBase::PutInputPacket(PacketPtr packet, size_t input_index) {
  FXL_DCHECK(input_index == 0);
  async::PostTask(worker_loop_.async(),
                  [this, packet] { TransformPacket(packet); });
}

bool FfmpegDecoderBase::can_accept_allocator_for_output(
    size_t output_index) const {
  FXL_DCHECK(output_index == 0);
  return true;
}

void FfmpegDecoderBase::SetAllocatorForOutput(
    std::shared_ptr<PayloadAllocator> allocator, size_t output_index) {
  FXL_DCHECK(output_index == 0);
  allocator_ = allocator;
}

void FfmpegDecoderBase::RequestOutputPacket() {
  flushing_ = false;

  State expected = State::kIdle;
  if (state_.compare_exchange_strong(expected, State::kOutputPacketRequested)) {
    stage()->RequestInputPacket();
  }
}

void FfmpegDecoderBase::TransformPacket(PacketPtr input) {
  if (flushing_) {
    // We got a flush request. Throw away the packet.
    return;
  }

  if (input->end_of_stream()) {
    state_ = State::kEndOfStream;
  }

  TRACE_DURATION("motown", (av_codec_context_->codec_type == AVMEDIA_TYPE_VIDEO
                                ? "DecodeVideoPacket"
                                : "DecodeAudioPacket"));
  FXL_DCHECK(input);
  FXL_DCHECK(allocator_);

  if (input->size() == 0 && !input->end_of_stream()) {
    // Throw away empty packets that aren't end-of-stream packets. The
    // underlying decoder interprets an empty packet as end-of-stream.
    stage()->RequestInputPacket();
    return;
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

  int64_t start_time = media::Timeline::local_now();

  int result = avcodec_send_packet(av_codec_context_.get(), &av_packet);

  if (result != 0) {
    FXL_DLOG(ERROR) << "avcodec_send_packet failed " << result;
    if (input->end_of_stream()) {
      // The input packet was end-of-stream. We won't get called again before
      // a flush, so make sure the output gets an end-of-stream packet.
      stage()->PutOutputPacket(CreateEndOfStreamPacket());
    }

    return;
  }

  while (true) {
    int result =
        avcodec_receive_frame(av_codec_context_.get(), av_frame_ptr_.get());

    if (result != 0) {
      decode_duration_.AddSample(media::Timeline::local_now() - start_time);
    }

    switch (result) {
      case 0:
        // Succeeded, frame produced.
        {
          PacketPtr packet = CreateOutputPacket(*av_frame_ptr_, allocator_);
          av_frame_unref(av_frame_ptr_.get());

          // If the state is still |kOutputPacketRequested|, set it to |kIdle|.
          // It could be |kIdle| already if a flush occurred or |kEndOfStream|
          // if we got an end-of-stream packet. In either of those cases, we
          // want to leave the state unchanged.
          State expected = State::kOutputPacketRequested;
          state_.compare_exchange_strong(expected, State::kIdle);

          stage()->PutOutputPacket(packet);
        }

        // Loop around to call avcodec_receive_frame again.
        break;

      case AVERROR(EAGAIN):
        // Succeeded, no frame produced, need another input packet.
        FXL_DCHECK(input->size() != 0);

        if (!input->end_of_stream()) {
          if (state_ == State::kOutputPacketRequested) {
            stage()->RequestInputPacket();
          }

          return;
        }

        // The input packet is an end-of-stream packet, but it has payload. The
        // underlying decoder interprets an empty packet as end-of-stream, so
        // we need to send it an empty packet. We do this by reentering
        // |TransformPacket|. This is safe, because we get |AVERROR_EOF|, not
        // |AVERROR(EAGAIN)| when the decoder is drained following an empty
        // input packet.
        TransformPacket(CreateEndOfStreamPacket());
        return;

      case AVERROR_EOF:
        // Succeeded, no frame produced, end-of-stream sequence complete.
        FXL_DCHECK(input->end_of_stream());
        stage()->PutOutputPacket(CreateEndOfStreamPacket());
        return;

      default:
        FXL_DLOG(ERROR) << "avcodec_receive_frame failed " << result;
        if (input->end_of_stream()) {
          // The input packet was end-of-stream. We won't get called again
          // before a flush, so make sure the output gets an end-of-stream
          // packet.
          stage()->PutOutputPacket(CreateEndOfStreamPacket());
        }

        return;
    }
  }
}

void FfmpegDecoderBase::OnNewInputPacket(const PacketPtr& packet) {}

// static
int FfmpegDecoderBase::AllocateBufferForAvFrame(
    AVCodecContext* av_codec_context, AVFrame* av_frame, int flags) {
  // It's important to use av_codec_context here rather than context(),
  // because av_codec_context is different for different threads when we're
  // decoding on multiple threads. Be sure to avoid using self->context() or
  // self->av_codec_context_.

  // CODEC_CAP_DR1 is required in order to do allocation this way.
  FXL_DCHECK(av_codec_context->codec->capabilities & CODEC_CAP_DR1);

  FfmpegDecoderBase* self =
      reinterpret_cast<FfmpegDecoderBase*>(av_codec_context->opaque);
  FXL_DCHECK(self);
  FXL_DCHECK(self->allocator_);

  return self->BuildAVFrame(*av_codec_context, av_frame,
                            self->allocator_.get());
}

// static
void FfmpegDecoderBase::ReleaseBufferForAvFrame(void* opaque, uint8_t* buffer) {
  FXL_DCHECK(opaque);
  FXL_DCHECK(buffer);
  PayloadAllocator* allocator = reinterpret_cast<PayloadAllocator*>(opaque);
  allocator->ReleasePayloadBuffer(buffer);
}

PacketPtr FfmpegDecoderBase::CreateEndOfStreamPacket() {
  return Packet::CreateEndOfStream(next_pts_, pts_rate_);
}

FfmpegDecoderBase::DecoderPacket::~DecoderPacket() {
  FXL_DCHECK(owner_);
  async::PostTask(owner_->worker_loop_.async(),
                  [av_buffer_ref = av_buffer_ref_]() mutable {
                    av_buffer_unref(&av_buffer_ref);
                  });
}

void FfmpegDecoderBase::Dump(std::ostream& os) const {
  os << label() << indent;
  stage()->Dump(os);
  os << newl << "output stream type:" << output_stream_type();
  os << newl << "state:             ";

  switch (state_) {
    case State::kIdle:
      os << "idle";
      break;
    case State::kOutputPacketRequested:
      os << "output packet requested";
      break;
    case State::kEndOfStream:
      os << "end of stream";
      break;
  }

  os << newl << "flushing:          " << flushing_;
  os << newl << "next pts:          " << AsNs(next_pts_) << "@" << pts_rate_;

  if (decode_duration_.count() != 0) {
    os << newl << "decodes:           " << decode_duration_.count();
    os << newl << "decode durations:";
    os << newl << "    minimum        " << AsNs(decode_duration_.min());
    os << newl << "    average        " << AsNs(decode_duration_.average());
    os << newl << "    maximum        " << AsNs(decode_duration_.max());
  }

  os << outdent;
}

}  // namespace media_player
