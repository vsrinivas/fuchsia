// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_FFMPEG_FFMPEG_AUDIO_DECODER_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_FFMPEG_FFMPEG_AUDIO_DECODER_H_

#include <memory>

#include "lib/media/timeline/timeline_rate.h"
#include "src/media/playback/mediaplayer_tmp/ffmpeg/ffmpeg_decoder_base.h"
#include "src/media/playback/mediaplayer_tmp/ffmpeg/lpcm_util.h"

namespace media_player {

// Decoder implementation employing an ffmpeg audio decoder.
class FfmpegAudioDecoder : public FfmpegDecoderBase {
 public:
  static std::shared_ptr<Decoder> Create(AvCodecContextPtr av_codec_context);

  FfmpegAudioDecoder(AvCodecContextPtr av_codec_context);

  ~FfmpegAudioDecoder() override;

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
  // Align sample buffers on 32-byte boundaries. This is the value that
  // Chromium uses and is supposed to work for all processor architectures.
  // Strangely, if we were to tell ffmpeg to use the default (by passing 0),
  // it aligns on 32 sample (not byte) boundaries.
  static const int kChannelAlign = 32;

  // For interleaving, if needed.
  std::unique_ptr<LpcmUtil> lpcm_util_;

  // For interleaving, if needed.
  std::unique_ptr<StreamType> stream_type_;
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_FFMPEG_FFMPEG_AUDIO_DECODER_H_
