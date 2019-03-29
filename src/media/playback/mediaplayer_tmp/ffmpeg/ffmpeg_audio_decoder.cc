// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer_tmp/ffmpeg/ffmpeg_audio_decoder.h"

#include "src/lib/fxl/logging.h"
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

  stream_type_ = std::move(stream_type);

  if (av_sample_fmt_is_planar(context()->sample_fmt)) {
    // Prepare for interleaving.
    lpcm_util_ = LpcmUtil::Create(*stream_type_->audio());
  }
}

FfmpegAudioDecoder::~FfmpegAudioDecoder() {}

void FfmpegAudioDecoder::ConfigureConnectors() {
  ConfigureInputToUseLocalMemory(0, 2);
  // TODO(dalesat): Real numbers here. How big are packets?
  // We're OK for now, because the audio renderer asks for a single VMO that's
  // big enough to handle any packet we want to produce.
  ConfigureOutputToUseLocalMemory(0, 1, 1);
}

void FfmpegAudioDecoder::OnNewInputPacket(const PacketPtr& packet) {
  if (next_pts() == Packet::kNoPts) {
    set_next_pts(packet->GetPts(pts_rate()));
  }

  context()->reordered_opaque = packet->discontinuity() ? 1 : 0;
}

int FfmpegAudioDecoder::BuildAVFrame(const AVCodecContext& av_codec_context,
                                     AVFrame* av_frame) {
  FXL_DCHECK(av_frame);

  AVSampleFormat av_sample_format =
      static_cast<AVSampleFormat>(av_frame->format);

  int buffer_size = av_samples_get_buffer_size(
      &av_frame->linesize[0], av_codec_context.channels, av_frame->nb_samples,
      av_sample_format, FfmpegAudioDecoder::kChannelAlign);
  if (buffer_size < 0) {
    FXL_LOG(WARNING) << "av_samples_get_buffer_size failed";
    return buffer_size;
  }

  // Get the right payload buffer. If we need to interleave later, we just get
  // a buffer allocated using malloc. If not, we ask the stage for a buffer.
  fbl::RefPtr<PayloadBuffer> buffer =
      lpcm_util_ ? PayloadBuffer::CreateWithMalloc(buffer_size)
                 : AllocatePayloadBuffer(buffer_size);

  if (!buffer) {
    // TODO(dalesat): Renderer VMO is full. What can we do about this?
    FXL_LOG(FATAL) << "Ran out of memory for decoded audio.";
  }

  // Check that the allocator has met the common alignment requirements and
  // that those requirements are good enough for the decoder.
  FXL_DCHECK(PayloadBuffer::IsAligned(buffer->data()));
  FXL_DCHECK(PayloadBuffer::kByteAlignment >= kChannelAlign);

  if (!av_sample_fmt_is_planar(av_sample_format)) {
    // Samples are interleaved. There's just one buffer.
    av_frame->data[0] = reinterpret_cast<uint8_t*>(buffer->data());
  } else {
    // Samples are not interleaved. There's one buffer per channel.
    int channels = av_codec_context.channels;
    int bytes_per_channel = buffer_size / channels;
    uint8_t* channel_buffer = reinterpret_cast<uint8_t*>(buffer->data());

    FXL_DCHECK(buffer || bytes_per_channel == 0);

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

  av_frame->buf[0] = CreateAVBuffer(std::move(buffer));
  av_frame->reordered_opaque = av_codec_context.reordered_opaque;

  return 0;
}

PacketPtr FfmpegAudioDecoder::CreateOutputPacket(
    const AVFrame& av_frame, fbl::RefPtr<PayloadBuffer> payload_buffer) {
  FXL_DCHECK(av_frame.buf[0]);
  FXL_DCHECK(payload_buffer);

  // We infer the PTS for a packet based on the assumption that the decoder
  // produces an uninterrupted stream of frames. The PTS value in av_frame is
  // often bogus, and we get bad results if we try to use it. This approach is
  // consistent with the way Chromium deals with the ffmpeg audio decoders.
  int64_t pts = next_pts();

  if (pts != Packet::kNoPts) {
    set_next_pts(pts + av_frame.nb_samples);
  }

  FXL_DCHECK(stream_type_);
  FXL_DCHECK(stream_type_->audio());

  uint64_t payload_size =
      stream_type_->audio()->min_buffer_size(av_frame.nb_samples);

  if (lpcm_util_) {
    // We need to interleave. The non-interleaved frames are in
    // |payload_buffer|, which was allocated from system memory. That buffer
    // will get released later in ReleaseBufferForAvFrame. We need a new
    // buffer for the interleaved frames, which we get from the stage.
    auto new_payload_buffer = AllocatePayloadBuffer(payload_size);
    if (!new_payload_buffer) {
      // TODO(dalesat): Renderer VMO is full. What can we do about this?
      FXL_LOG(FATAL) << "Ran out of memory for decoded, interleaved audio.";
    }

    lpcm_util_->Interleave(
        payload_buffer->data(),
        av_frame.linesize[0] * stream_type_->audio()->channels(),
        new_payload_buffer->data(), av_frame.nb_samples);

    // |new_payload_buffer| is the buffer we want to attach to the |Packet|.
    // This assignment drops the reference to the original |payload_buffer|, so
    // it will be recycled once the |AVBuffer| is released.
    payload_buffer = std::move(new_payload_buffer);
  }

  // Create the output packet. We set the discontinuity bit on the packet if
  // the corresponding input packet had one.
  return Packet::Create(
      pts, pts_rate(),
      false,                           // Not a keyframe
      av_frame.reordered_opaque != 0,  // discontinuity
      false,  // Not end-of-stream. The base class handles end-of-stream.
      payload_size, std::move(payload_buffer));
}

const char* FfmpegAudioDecoder::label() const { return "audio_decoder"; }

}  // namespace media_player
