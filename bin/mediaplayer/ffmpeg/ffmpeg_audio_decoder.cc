// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/ffmpeg/ffmpeg_audio_decoder.h"

#include "lib/fxl/logging.h"
#include "lib/media/timeline/timeline.h"
#include "lib/media/timeline/timeline_rate.h"

namespace media_player {

// static
std::shared_ptr<Decoder> FfmpegAudioDecoder::Create(
    AvCodecContextPtr av_codec_context) {
  return std::make_shared<FfmpegAudioDecoder>(std::move(av_codec_context));
}

FfmpegAudioDecoder::FfmpegAudioDecoder(AvCodecContextPtr av_codec_context)
    : FfmpegDecoderBase(std::move(av_codec_context)) {
  FXL_DCHECK(context());
  FXL_DCHECK(context()->channels > 0);

  std::unique_ptr<StreamType> stream_type = output_stream_type();
  FXL_DCHECK(stream_type);
  FXL_DCHECK(stream_type->audio());
  set_pts_rate(
      media::TimelineRate(stream_type->audio()->frames_per_second(), 1));

  if (av_sample_fmt_is_planar(context()->sample_fmt)) {
    // Prepare for interleaving.
    stream_type_ = std::move(stream_type);
    lpcm_util_ = LpcmUtil::Create(*stream_type_->audio());
    default_allocator_ = PayloadAllocator::CreateDefault();
  }
}

FfmpegAudioDecoder::~FfmpegAudioDecoder() {}

void FfmpegAudioDecoder::OnNewInputPacket(const PacketPtr& packet) {
  incoming_pts_rate_ = packet->pts_rate();

  if (next_pts() == Packet::kUnknownPts) {
    if (packet->pts() == Packet::kUnknownPts) {
      FXL_DLOG(WARNING) << "No PTS established, using 0 by default.";
      set_next_pts(0);
    } else {
      set_next_pts(packet->GetPts(pts_rate()));
    }
  }
}

int FfmpegAudioDecoder::BuildAVFrame(
    const AVCodecContext& av_codec_context, AVFrame* av_frame,
    const std::shared_ptr<PayloadAllocator>& allocator) {
  FXL_DCHECK(av_frame);
  FXL_DCHECK(allocator);

  // Use the provided allocator unless we intend to interleave later, in which
  // case use the default allocator. We'll interleave into a buffer from the
  // provided allocator in CreateOutputPacket.
  const std::shared_ptr<PayloadAllocator>& allocator_to_use =
      (lpcm_util_ == nullptr) ? allocator : default_allocator_;
  FXL_DCHECK(allocator_to_use);

  AVSampleFormat av_sample_format =
      static_cast<AVSampleFormat>(av_frame->format);

  int buffer_size = av_samples_get_buffer_size(
      &av_frame->linesize[0], av_codec_context.channels, av_frame->nb_samples,
      av_sample_format, FfmpegAudioDecoder::kChannelAlign);
  if (buffer_size < 0) {
    FXL_LOG(WARNING) << "av_samples_get_buffer_size failed";
    return buffer_size;
  }

  uint8_t* buffer = static_cast<uint8_t*>(
      allocator_to_use->AllocatePayloadBuffer(buffer_size));
  if (!buffer) {
    // TODO(dalesat): Renderer VMO is full. What can we do about this?
    FXL_LOG(FATAL) << "Ran out of memory for decoded audio.";
  }

  if (!av_sample_fmt_is_planar(av_sample_format)) {
    // Samples are interleaved. There's just one buffer.
    av_frame->data[0] = buffer;
  } else {
    // Samples are not interleaved. There's one buffer per channel.
    int channels = av_codec_context.channels;
    int bytes_per_channel = buffer_size / channels;
    uint8_t* channel_buffer = buffer;

    FXL_DCHECK(buffer != nullptr || bytes_per_channel == 0);

    if (channels <= AV_NUM_DATA_POINTERS) {
      // The buffer pointers will fit in av_frame->data.
      FXL_DCHECK(av_frame->extended_data == av_frame->data);
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

  av_frame->buf[0] = CreateAVBuffer(buffer, static_cast<size_t>(buffer_size),
                                    allocator_to_use);

  return 0;
}

PacketPtr FfmpegAudioDecoder::CreateOutputPacket(
    const AVFrame& av_frame,
    const std::shared_ptr<PayloadAllocator>& allocator) {
  FXL_DCHECK(allocator);

  // We infer the PTS for a packet based on the assumption that the decoder
  // produces an uninterrupted stream of frames. The PTS value in av_frame is
  // often bogus, and we get bad results if we try to use it. This approach is
  // consistent with the way Chromium deals with the ffmpeg audio decoders.
  int64_t pts = next_pts();

  set_next_pts(pts + av_frame.nb_samples);

  if (lpcm_util_) {
    // We need to interleave. The non-interleaved frames are in a buffer that
    // was allocated from the default allocator. That buffer will get released
    // later in ReleaseBufferForAvFrame. We need a new buffer for the
    // interleaved frames, which we get from the provided allocator.
    FXL_DCHECK(stream_type_);
    FXL_DCHECK(stream_type_->audio());
    uint64_t payload_size =
        stream_type_->audio()->min_buffer_size(av_frame.nb_samples);
    void* payload_buffer = allocator->AllocatePayloadBuffer(payload_size);
    if (!payload_buffer) {
      // TODO(dalesat): Renderer VMO is full. What can we do about this?
      FXL_LOG(FATAL) << "Ran out of memory for decoded, interleaved audio.";
    }

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
                                 av_buffer_ref(av_frame.buf[0]), this);
  }
}

const char* FfmpegAudioDecoder::label() const { return "audio_decoder"; }

}  // namespace media_player
