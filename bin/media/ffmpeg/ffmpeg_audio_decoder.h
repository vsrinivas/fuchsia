// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "garnet/bin/media/audio/lpcm_util.h"
#include "garnet/bin/media/ffmpeg/ffmpeg_decoder_base.h"
#include "lib/media/timeline/timeline_rate.h"

namespace media {

// Decoder implementation employing an ffmpeg audio decoder.
class FfmpegAudioDecoder : public FfmpegDecoderBase {
 public:
  FfmpegAudioDecoder(AvCodecContextPtr av_codec_context);

  ~FfmpegAudioDecoder() override;

 protected:
  // FfmpegDecoderBase overrides.
  void OnNewInputPacket(const PacketPtr& packet) override;

  int BuildAVFrame(const AVCodecContext& av_codec_context,
                   AVFrame* av_frame,
                   PayloadAllocator* allocator) override;

  PacketPtr CreateOutputPacket(const AVFrame& av_frame,
                               PayloadAllocator* allocator) override;

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

  // PTS rate from incoming packet.
  TimelineRate incoming_pts_rate_;
};

}  // namespace media
