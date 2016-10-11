// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/media/src/audio/lpcm_util.h"
#include "apps/media/src/ffmpeg/ffmpeg_decoder_base.h"

namespace mojo {
namespace media {

// Decoder implementation employing an ffmpeg audio decoder.
class FfmpegAudioDecoder : public FfmpegDecoderBase {
 public:
  FfmpegAudioDecoder(AvCodecContextPtr av_codec_context);

  ~FfmpegAudioDecoder() override;

 protected:
  // FfmpegDecoderBase overrides.
  void Flush() override;

  int Decode(const AVPacket& av_packet,
             const ffmpeg::AvFramePtr& av_frame_ptr,
             PayloadAllocator* allocator,
             bool* frame_decoded_out) override;

  PacketPtr CreateOutputPacket(const AVFrame& av_frame,
                               PayloadAllocator* allocator) override;

  PacketPtr CreateOutputEndOfStreamPacket() override;

 private:
  // Align sample buffers on 32-byte boundaries. This is the value that Chromium
  // uses and is supposed to work for all processor architectures. Strangely, if
  // we were to tell ffmpeg to use the default (by passing 0), it aligns on 32
  // sample (not byte) boundaries.
  static const int kChannelAlign = 32;

  // Callback used by the ffmpeg decoder to acquire a buffer.
  static int AllocateBufferForAvFrame(AVCodecContext* av_codec_context,
                                      AVFrame* av_frame,
                                      int flags);

  // Callback used by the ffmpeg decoder to release a buffer.
  static void ReleaseBufferForAvFrame(void* opaque, uint8_t* buffer);

  // The allocator used by avcodec_decode_audio4 to provide context for
  // AllocateBufferForAvFrame. This is set only during the call to
  // avcodec_decode_audio4.
  PayloadAllocator* allocator_;

  // For interleaving, if needed.
  std::unique_ptr<LpcmUtil> lpcm_util_;

  // For interleaving, if needed.
  std::unique_ptr<StreamType> stream_type_;

  // Used to supply missing PTS.
  int64_t next_pts_ = Packet::kUnknownPts;

  TimelineRate pts_rate_;
};

}  // namespace media
}  // namespace mojo
