// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/ffmpeg/ffmpeg_decoder_base.h"

#include "apps/media/src/ffmpeg/av_codec_context.h"
#include "lib/ftl/logging.h"

namespace mojo {
namespace media {

FfmpegDecoderBase::FfmpegDecoderBase(AvCodecContextPtr av_codec_context)
    : av_codec_context_(std::move(av_codec_context)),
      av_frame_ptr_(ffmpeg::AvFrame::Create()) {
  FTL_DCHECK(av_codec_context_);
}

FfmpegDecoderBase::~FfmpegDecoderBase() {}

std::unique_ptr<StreamType> FfmpegDecoderBase::output_stream_type() {
  return AvCodecContext::GetStreamType(*av_codec_context_);
}

void FfmpegDecoderBase::Flush() {
  FTL_DCHECK(av_codec_context_);
  avcodec_flush_buffers(av_codec_context_.get());
}

bool FfmpegDecoderBase::TransformPacket(const PacketPtr& input,
                                        bool new_input,
                                        PayloadAllocator* allocator,
                                        PacketPtr* output) {
  FTL_DCHECK(input);
  FTL_DCHECK(allocator);
  FTL_DCHECK(output);

  *output = nullptr;

  if (new_input) {
    PrepareInputPacket(input);
  }

  bool frame_decoded = false;
  int input_bytes_used =
      Decode(av_packet_, av_frame_ptr_, allocator, &frame_decoded);
  if (input_bytes_used < 0) {
    // Decode failed.
    return UnprepareInputPacket(input, output);
  }

  if (frame_decoded) {
    FTL_DCHECK(allocator);
    *output = CreateOutputPacket(*av_frame_ptr_, allocator);
    av_frame_unref(av_frame_ptr_.get());
  }

  FTL_CHECK(input_bytes_used <= av_packet_.size)
      << "Ffmpeg decoder read beyond end of packet";
  av_packet_.size -= input_bytes_used;
  av_packet_.data += input_bytes_used;

  if (av_packet_.size != 0 || (input->end_of_stream() && frame_decoded)) {
    // The input packet is only partially decoded, or it's an end-of-stream
    // packet and we're still draining. Let the caller know we want to see the
    // input packet again.
    return false;
  }

  // Used up the whole input packet, and, if we were draining, we're done with
  // that too.
  return UnprepareInputPacket(input, output);
}

void FfmpegDecoderBase::PrepareInputPacket(const PacketPtr& input) {
  av_init_packet(&av_packet_);
  av_packet_.data = reinterpret_cast<uint8_t*>(input->payload());
  av_packet_.size = input->size();
  av_packet_.pts = input->pts();
}

bool FfmpegDecoderBase::UnprepareInputPacket(const PacketPtr& input,
                                             PacketPtr* output) {
  if (input->end_of_stream()) {
    // Indicate end of stream. This happens when we're draining for the last
    // time, so there should be no output packet yet.
    FTL_DCHECK(*output == nullptr);
    *output = CreateOutputEndOfStreamPacket();
  }

  av_packet_.size = 0;
  av_packet_.data = nullptr;

  return true;
}

FfmpegDecoderBase::DecoderPacket::~DecoderPacket() {
  av_buffer_unref(&av_buffer_ref_);
}

void FfmpegDecoderBase::DecoderPacket::Release() {
  delete this;
}

}  // namespace media
}  // namespace mojo
