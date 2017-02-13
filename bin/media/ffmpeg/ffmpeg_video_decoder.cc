// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/ffmpeg/ffmpeg_video_decoder.h"

#include <algorithm>

#include "apps/media/lib/timeline/timeline.h"
#include "apps/media/lib/timeline/timeline_rate.h"
#include "apps/media/src/ffmpeg/ffmpeg_formatting.h"
#include "lib/ftl/logging.h"
extern "C" {
#include "third_party/ffmpeg/libavutil/imgutils.h"
}

namespace media {

FfmpegVideoDecoder::FfmpegVideoDecoder(AvCodecContextPtr av_codec_context)
    : FfmpegDecoderBase(std::move(av_codec_context)) {
  FTL_DCHECK(context());

  context()->opaque = this;
  context()->get_buffer2 = AllocateBufferForAvFrame;
  context()->refcounted_frames = 1;

  // Turn on multi-proc decoding by allowing the decoder to use three threads
  // (the calling thread and the two specified here). FF_THREAD_FRAME means
  // that threads are assigned an entire frame.
  // TODO(dalesat): Consider using FF_THREAD_SLICE.
  context()->thread_count = 2;
  context()->thread_type = FF_THREAD_FRAME;

  // Precalculate the PTS rate needed for packets, if possible.
  if (context()->time_base.num != 0) {
    pts_rate_ =
        TimelineRate(context()->time_base.den, context()->time_base.num);
  }

  // Determine the frame layout we will use.
  frame_buffer_size_ = LayoutFrame(
      PixelFormatFromAVPixelFormat(context()->pix_fmt),
      VideoStreamType::Extent(context()->coded_width, context()->coded_height),
      &line_stride_, &plane_offset_);
}

FfmpegVideoDecoder::~FfmpegVideoDecoder() {}

int FfmpegVideoDecoder::Decode(const AVPacket& av_packet,
                               const ffmpeg::AvFramePtr& av_frame_ptr,
                               PayloadAllocator* allocator,
                               const PacketPtr& original_input_packet,
                               bool* frame_decoded_out) {
  FTL_DCHECK(allocator);
  FTL_DCHECK(frame_decoded_out);
  FTL_DCHECK(context());
  FTL_DCHECK(av_frame_ptr);

  FTL_DCHECK(av_packet.pts != AV_NOPTS_VALUE);

  if (pts_rate_ == TimelineRate::Zero) {
    pts_rate_ = original_input_packet->pts_rate();
  }

  // Use the provided allocator (for allocations in AllocateBufferForAvFrame).
  allocator_ = allocator;

  // We put the pts here so it can be recovered later in CreateOutputPacket.
  // Ffmpeg deals with the frame ordering issues.
  context()->reordered_opaque = av_packet.pts;

  int frame_decoded = 0;
  int input_bytes_used = avcodec_decode_video2(
      context().get(), av_frame_ptr.get(), &frame_decoded, &av_packet);
  *frame_decoded_out = frame_decoded != 0;

  // We're done with this allocator.
  allocator_ = nullptr;

  return input_bytes_used;
}

void FfmpegVideoDecoder::Flush() {
  FfmpegDecoderBase::Flush();
  next_pts_ = Packet::kUnknownPts;
}

PacketPtr FfmpegVideoDecoder::CreateOutputPacket(const AVFrame& av_frame,
                                                 PayloadAllocator* allocator) {
  FTL_DCHECK(allocator);

  // Recover the pts deposited in Decode.
  next_pts_ = av_frame.reordered_opaque;

  return DecoderPacket::Create(next_pts_, pts_rate_, av_frame.key_frame,
                               av_buffer_ref(av_frame.buf[0]));
}

PacketPtr FfmpegVideoDecoder::CreateOutputEndOfStreamPacket() {
  return Packet::CreateEndOfStream(next_pts_, pts_rate_);
}

int FfmpegVideoDecoder::AllocateBufferForAvFrame(
    AVCodecContext* av_codec_context,
    AVFrame* av_frame,
    int flags) {
  // It's important to use av_codec_context here rather than context(),
  // because av_codec_context is different for different threads when we're
  // decoding on multiple threads. Be sure to avoid using self->context().

  // CODEC_CAP_DR1 is required in order to do allocation this way.
  FTL_DCHECK(av_codec_context->codec->capabilities & CODEC_CAP_DR1);

  FfmpegVideoDecoder* self =
      reinterpret_cast<FfmpegVideoDecoder*>(av_codec_context->opaque);
  FTL_DCHECK(self);
  FTL_DCHECK(self->allocator_);

  Extent visible_size(av_codec_context->width, av_codec_context->height);
  const int result =
      av_image_check_size(visible_size.width(), visible_size.height(), 0, NULL);
  if (result < 0) {
    return result;
  }

  // FFmpeg has specific requirements on the allocation size of the frame.  The
  // following logic replicates FFmpeg's allocation strategy to ensure buffers
  // are not overread / overwritten.  See ff_init_buffer_info() for details.

  // When lowres is non-zero, dimensions should be divided by 2^(lowres), but
  // since we don't use this, just FTL_DCHECK that it's zero.
  FTL_DCHECK(av_codec_context->lowres == 0);
  Extent coded_size(
      std::max(visible_size.width(),
               static_cast<uint32_t>(av_codec_context->coded_width)),
      std::max(visible_size.height(),
               static_cast<uint32_t>(av_codec_context->coded_height)));

  uint8_t* buffer = static_cast<uint8_t*>(
      self->allocator_->AllocatePayloadBuffer(self->frame_buffer_size_));

  // TODO(dalesat): For investigation purposes only...remove one day.
  if (self->first_frame_) {
    self->first_frame_ = false;
    self->colorspace_ = av_codec_context->colorspace;
    self->coded_size_ = coded_size;
  } else {
    if (av_codec_context->colorspace != self->colorspace_) {
      FTL_LOG(WARNING) << " colorspace changed to "
                       << av_codec_context->colorspace << std::endl;
    }
    if (coded_size.width() != self->coded_size_.width()) {
      FTL_LOG(WARNING) << " coded_size width changed to " << coded_size.width()
                       << std::endl;
    }
    if (coded_size.height() != self->coded_size_.height()) {
      FTL_LOG(WARNING) << " coded_size height changed to "
                       << coded_size.height() << std::endl;
    }
    self->colorspace_ = av_codec_context->colorspace;
    self->coded_size_ = coded_size;
  }

  if (buffer == nullptr) {
    FTL_LOG(ERROR) << "failed to allocate buffer of size "
                   << self->frame_buffer_size_;
    return -1;
  }

  // Decoders require a zeroed buffer.
  std::memset(buffer, 0, self->frame_buffer_size_);

  FTL_DCHECK(self->line_stride_.size() == self->plane_offset_.size());

  for (size_t plane = 0; plane < self->plane_offset_.size(); ++plane) {
    av_frame->data[plane] = buffer + self->plane_offset_[plane];
    av_frame->linesize[plane] = self->line_stride_[plane];
  }

  // TODO(dalesat): Do we need to attach colorspace info to the packet?

  av_frame->width = coded_size.width();
  av_frame->height = coded_size.height();
  av_frame->format = av_codec_context->pix_fmt;
  av_frame->reordered_opaque = av_codec_context->reordered_opaque;

  FTL_DCHECK(av_frame->data[0] == buffer);
  av_frame->buf[0] = av_buffer_create(buffer, self->frame_buffer_size_,
                                      ReleaseBufferForAvFrame, self->allocator_,
                                      0);  // flags

  return 0;
}

void FfmpegVideoDecoder::ReleaseBufferForAvFrame(void* opaque,
                                                 uint8_t* buffer) {
  FTL_DCHECK(opaque);
  FTL_DCHECK(buffer);
  PayloadAllocator* allocator = reinterpret_cast<PayloadAllocator*>(opaque);
  allocator->ReleasePayloadBuffer(buffer);
}

}  // namespace media
