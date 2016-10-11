// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/ffmpeg/ffmpeg_audio_decoder.h"

#include "lib/ftl/logging.h"

namespace mojo {
namespace media {

FfmpegAudioDecoder::FfmpegAudioDecoder(AvCodecContextPtr av_codec_context)
    : FfmpegDecoderBase(std::move(av_codec_context)) {
  FTL_DCHECK(context());
  FTL_DCHECK(context()->channels > 0);

  context()->opaque = this;
  context()->get_buffer2 = AllocateBufferForAvFrame;
  context()->refcounted_frames = 1;

  std::unique_ptr<StreamType> stream_type = output_stream_type();
  FTL_DCHECK(stream_type_->audio());
  pts_rate_ = TimelineRate(stream_type_->audio()->frames_per_second(), 1);

  if (av_sample_fmt_is_planar(context()->sample_fmt)) {
    // Prepare for interleaving.
    stream_type_ = std::move(stream_type);
    lpcm_util_ = LpcmUtil::Create(*stream_type_->audio());
  }
}

FfmpegAudioDecoder::~FfmpegAudioDecoder() {}

void FfmpegAudioDecoder::Flush() {
  FfmpegDecoderBase::Flush();
  next_pts_ = Packet::kUnknownPts;
}

int FfmpegAudioDecoder::Decode(const AVPacket& av_packet,
                               const ffmpeg::AvFramePtr& av_frame_ptr,
                               PayloadAllocator* allocator,
                               bool* frame_decoded_out) {
  FTL_DCHECK(allocator);
  FTL_DCHECK(frame_decoded_out);
  FTL_DCHECK(context());
  FTL_DCHECK(av_frame_ptr);

  if (next_pts_ == Packet::kUnknownPts) {
    if (av_packet.pts == AV_NOPTS_VALUE) {
      next_pts_ = 0;
    } else {
      next_pts_ = av_packet.pts;
    }
  }

  // Use the provided allocator (for allocations in AllocateBufferForAvFrame)
  // unless we intend to interleave later, in which case use the default
  // allocator. We'll interleave into a buffer from the provided allocator
  // in CreateOutputPacket.
  allocator_ = lpcm_util_ ? PayloadAllocator::GetDefault() : allocator;

  int frame_decoded = 0;
  int input_bytes_used = avcodec_decode_audio4(
      context().get(), av_frame_ptr.get(), &frame_decoded, &av_packet);
  *frame_decoded_out = frame_decoded != 0;

  // We're done with this allocator.
  allocator_ = nullptr;

  return input_bytes_used;
}

PacketPtr FfmpegAudioDecoder::CreateOutputPacket(const AVFrame& av_frame,
                                                 PayloadAllocator* allocator) {
  FTL_DCHECK(allocator);

  int64_t pts = av_frame.pts;
  if (pts == AV_NOPTS_VALUE) {
    pts = next_pts_;
    next_pts_ += av_frame.nb_samples;
  } else {
    next_pts_ = pts;
  }

  if (lpcm_util_) {
    // We need to interleave. The non-interleaved frames are in a buffer that
    // was allocated from the default allocator. That buffer will get released
    // later in ReleaseBufferForAvFrame. We need a new buffer for the
    // interleaved frames, which we get from the provided allocator.
    FTL_DCHECK(stream_type_);
    FTL_DCHECK(stream_type_->audio());
    uint64_t payload_size =
        stream_type_->audio()->min_buffer_size(av_frame.nb_samples);
    void* payload_buffer = allocator->AllocatePayloadBuffer(payload_size);

    lpcm_util_->Interleave(av_frame.buf[0]->data, av_frame.buf[0]->size,
                           payload_buffer, av_frame.nb_samples);

    return Packet::Create(
        pts, pts_rate_,
        false,  // The base class is responsible for end-of-stream.
        payload_size, payload_buffer, allocator);
  } else {
    // We don't need to interleave. The interleaved frames are in a buffer that
    // was allocated from the correct allocator.
    return DecoderPacket::Create(pts, pts_rate_,
                                 av_buffer_ref(av_frame.buf[0]));
  }
}

PacketPtr FfmpegAudioDecoder::CreateOutputEndOfStreamPacket() {
  return Packet::CreateEndOfStream(next_pts_, pts_rate_);
}

int FfmpegAudioDecoder::AllocateBufferForAvFrame(
    AVCodecContext* av_codec_context,
    AVFrame* av_frame,
    int flags) {
  // CODEC_CAP_DR1 is required in order to do allocation this way.
  FTL_DCHECK(av_codec_context->codec->capabilities & CODEC_CAP_DR1);

  FfmpegAudioDecoder* self =
      reinterpret_cast<FfmpegAudioDecoder*>(av_codec_context->opaque);
  FTL_DCHECK(self);
  FTL_DCHECK(self->allocator_);

  AVSampleFormat av_sample_format =
      static_cast<AVSampleFormat>(av_frame->format);

  int buffer_size = av_samples_get_buffer_size(
      &av_frame->linesize[0], av_codec_context->channels, av_frame->nb_samples,
      av_sample_format, FfmpegAudioDecoder::kChannelAlign);
  if (buffer_size < 0) {
    FTL_LOG(WARNING) << "av_samples_get_buffer_size failed";
    return buffer_size;
  }

  uint8_t* buffer = static_cast<uint8_t*>(
      self->allocator_->AllocatePayloadBuffer(buffer_size));

  if (!av_sample_fmt_is_planar(av_sample_format)) {
    // Samples are interleaved. There's just one buffer.
    av_frame->data[0] = buffer;
  } else {
    // Samples are not interleaved. There's one buffer per channel.
    int channels = av_codec_context->channels;
    int bytes_per_channel = buffer_size / channels;
    uint8_t* channel_buffer = buffer;

    FTL_DCHECK(buffer != nullptr || bytes_per_channel == 0);

    if (channels <= AV_NUM_DATA_POINTERS) {
      // The buffer pointers will fit in av_frame->data.
      FTL_DCHECK(av_frame->extended_data == av_frame->data);
      for (int channel = 0; channel < channels; ++channel) {
        av_frame->data[channel] = channel_buffer;
        channel_buffer += bytes_per_channel;
      }
    } else {
      // Too many channels for av_frame->data. We have to use
      // av_frame->extended_data
      av_frame->extended_data = static_cast<uint8_t**>(
          av_malloc(channels * sizeof(*av_frame->extended_data)));

      // The first AV_NUM_DATA_POINTERS go in both data and extended_data.
      int channel = 0;
      for (; channel < AV_NUM_DATA_POINTERS; ++channel) {
        av_frame->extended_data[channel] = av_frame->data[channel] =
            channel_buffer;
        channel_buffer += bytes_per_channel;
      }

      // The rest go only in extended_data.
      for (; channel < channels; ++channel) {
        av_frame->extended_data[channel] = channel_buffer;
        channel_buffer += bytes_per_channel;
      }
    }
  }

  av_frame->buf[0] = av_buffer_create(buffer, buffer_size,
                                      ReleaseBufferForAvFrame, self->allocator_,
                                      0);  // flags

  return 0;
}

void FfmpegAudioDecoder::ReleaseBufferForAvFrame(void* opaque,
                                                 uint8_t* buffer) {
  FTL_DCHECK(opaque);
  FTL_DCHECK(buffer);
  PayloadAllocator* allocator = reinterpret_cast<PayloadAllocator*>(opaque);
  allocator->ReleasePayloadBuffer(buffer);
}

}  // namespace media
}  // namespace mojo
