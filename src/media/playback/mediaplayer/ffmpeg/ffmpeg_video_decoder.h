// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_FFMPEG_FFMPEG_VIDEO_DECODER_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_FFMPEG_FFMPEG_VIDEO_DECODER_H_

#include "lib/media/cpp/timeline_rate.h"
#include "src/media/playback/mediaplayer/ffmpeg/ffmpeg_decoder_base.h"

namespace media_player {

// Decoder implementation employing and ffmpeg video decoder.
class FfmpegVideoDecoder : public FfmpegDecoderBase {
 public:
  static std::shared_ptr<Decoder> Create(AvCodecContextPtr av_codec_context);

  FfmpegVideoDecoder(AvCodecContextPtr av_codec_context);

  ~FfmpegVideoDecoder() override;

  // Node implementation.
  void ConfigureConnectors() override;

 protected:
  // FfmpegDecoderBase overrides.
  void OnNewInputPacket(const PacketPtr& packet) override;

  int BuildAVFrame(const AVCodecContext& av_codec_context,
                   AVFrame* av_frame) override;

  PacketPtr CreateOutputPacket(
      const AVFrame& av_frame,
      fbl::RefPtr<PayloadBuffer> payload_buffer) override;

  const char* label() const override;

 private:
  // Frame buffers must be aligned on 32-byte boundaries to enable SIMD
  // operations.
  static const int kFrameBufferAlign = 32;

  // Indicates whether the decoder has a non-zero coded size.
  bool has_size() const { return aligned_width_ != 0 && aligned_height_ != 0; }

  // Updates |buffer_size_|, |aligned_width_| and |aligned_height_| based on
  // |av_codec_context|. Returns true if those values change, false if not.
  // Specifying a changed size is fine.  Specifying a changed pix_fmt is not.
  bool UpdateSize(const AVCodecContext& av_codec_context);

  size_t buffer_size_ = 0;
  uint32_t aligned_width_ = 0;
  uint32_t aligned_height_ = 0;

  size_t configured_output_buffer_size_ = 0;
  std::unique_ptr<StreamType> revised_stream_type_;
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_FFMPEG_FFMPEG_VIDEO_DECODER_H_
