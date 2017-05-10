// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/ffmpeg/ffmpeg_audio_decoder.h"

#include "apps/media/lib/timeline/timeline.h"
#include "apps/media/lib/timeline/timeline_rate.h"
#include "lib/ftl/logging.h"

namespace media {
namespace {

bool PtssRoughlyEqual(int64_t a,
                      TimelineRate a_rate,
                      int64_t b,
                      TimelineRate b_rate) {
  a = a *
      TimelineRate::Product(TimelineRate::NsPerSecond, a_rate.Inverse(), false);
  b = b *
      TimelineRate::Product(TimelineRate::NsPerSecond, b_rate.Inverse(), false);
  return std::abs(a - b) < Timeline::ns_from_ms(50);
}

}  // namespace

FfmpegAudioDecoder::FfmpegAudioDecoder(AvCodecContextPtr av_codec_context)
    : FfmpegDecoderBase(std::move(av_codec_context)) {
  FTL_DCHECK(context());
  FTL_DCHECK(context()->channels > 0);

  std::unique_ptr<StreamType> stream_type = output_stream_type();
  FTL_DCHECK(stream_type);
  FTL_DCHECK(stream_type->audio());
  set_pts_rate(TimelineRate(stream_type->audio()->frames_per_second(), 1));

  if (av_sample_fmt_is_planar(context()->sample_fmt)) {
    // Prepare for interleaving.
    stream_type_ = std::move(stream_type);
    lpcm_util_ = LpcmUtil::Create(*stream_type_->audio());
  }
}

FfmpegAudioDecoder::~FfmpegAudioDecoder() {}

void FfmpegAudioDecoder::OnNewInputPacket(const PacketPtr& packet) {
  incoming_pts_rate_ = packet->pts_rate();

  if (next_pts() == Packet::kUnknownPts) {
    if (packet->pts() == Packet::kUnknownPts) {
      FTL_DLOG(WARNING) << "No PTS established, using 0 by default.";
      set_next_pts(0);
    } else {
      set_next_pts(packet->GetPts(pts_rate()));
    }
  }
}

int FfmpegAudioDecoder::BuildAVFrame(const AVCodecContext& av_codec_context,
                                     AVFrame* av_frame,
                                     PayloadAllocator* allocator) {
  FTL_DCHECK(av_frame);
  FTL_DCHECK(allocator);

  // Use the provided allocator unless we intend to interleave later, in which
  // case use the default allocator. We'll interleave into a buffer from the
  // provided allocator in CreateOutputPacket.
  if (lpcm_util_ != nullptr) {
    allocator = PayloadAllocator::GetDefault();
  }

  AVSampleFormat av_sample_format =
      static_cast<AVSampleFormat>(av_frame->format);

  int buffer_size = av_samples_get_buffer_size(
      &av_frame->linesize[0], av_codec_context.channels, av_frame->nb_samples,
      av_sample_format, FfmpegAudioDecoder::kChannelAlign);
  if (buffer_size < 0) {
    FTL_LOG(WARNING) << "av_samples_get_buffer_size failed";
    return buffer_size;
  }

  uint8_t* buffer =
      static_cast<uint8_t*>(allocator->AllocatePayloadBuffer(buffer_size));

  if (!av_sample_fmt_is_planar(av_sample_format)) {
    // Samples are interleaved. There's just one buffer.
    av_frame->data[0] = buffer;
  } else {
    // Samples are not interleaved. There's one buffer per channel.
    int channels = av_codec_context.channels;
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

  av_frame->buf[0] =
      CreateAVBuffer(buffer, static_cast<size_t>(buffer_size), allocator);

  return 0;
}

PacketPtr FfmpegAudioDecoder::CreateOutputPacket(const AVFrame& av_frame,
                                                 PayloadAllocator* allocator) {
  FTL_DCHECK(allocator);

  int64_t pts;
  if (av_frame.pts == AV_NOPTS_VALUE) {
    // No PTS supplied. Assume we're progressing normally.
    pts = next_pts();
  } else if (incoming_pts_rate_ == pts_rate()) {
    // PTS supplied in the preferred units.
    pts = av_frame.pts;
  } else {
    // PTS isn't in preferred units. Assume we're progressing normally.
    // TODO(dalesat): Might need to reset if pts and av_frame.pts diverge.
    pts = next_pts();
    FTL_DCHECK(
        PtssRoughlyEqual(pts, pts_rate(), av_frame.pts, incoming_pts_rate_));
  }

  set_next_pts(pts + av_frame.nb_samples);

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
        pts, pts_rate(),
        false,  // Not a keyframe
        false,  // The base class is responsible for end-of-stream.
        payload_size, payload_buffer, allocator);
  } else {
    // We don't need to interleave. The interleaved frames are in a buffer that
    // was allocated from the correct allocator.
    return DecoderPacket::Create(pts, pts_rate(), false,
                                 av_buffer_ref(av_frame.buf[0]));
  }
}

}  // namespace media
