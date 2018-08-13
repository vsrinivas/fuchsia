// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_FFMPEG_FFMPEG_VIDEO_DECODER_H_
#define GARNET_BIN_MEDIAPLAYER_FFMPEG_FFMPEG_VIDEO_DECODER_H_

#include "garnet/bin/mediaplayer/ffmpeg/ffmpeg_decoder_base.h"
#include "garnet/bin/mediaplayer/ffmpeg/ffmpeg_video_frame_layout.h"
#include "lib/media/timeline/timeline_rate.h"

namespace media_player {

// Decoder implementation employing and ffmpeg video decoder.
class FfmpegVideoDecoder : public FfmpegDecoderBase {
 public:
  static std::shared_ptr<Decoder> Create(AvCodecContextPtr av_codec_context);

  FfmpegVideoDecoder(AvCodecContextPtr av_codec_context);

  ~FfmpegVideoDecoder() override;

 protected:
  // FfmpegDecoderBase overrides.
  void OnNewInputPacket(const PacketPtr& packet) override;

  int BuildAVFrame(const AVCodecContext& av_codec_context, AVFrame* av_frame,
                   const std::shared_ptr<PayloadAllocator>& allocator) override;

  PacketPtr CreateOutputPacket(
      const AVFrame& av_frame,
      const std::shared_ptr<PayloadAllocator>& allocator) override;

  const char* label() const override;

 private:
  FfmpegVideoFrameLayout frame_layout_;
  std::unique_ptr<StreamType> revised_stream_type_;

  // TODO(dalesat): For investigation only...remove these three fields.
  bool first_frame_ = true;
  AVColorSpace colorspace_;
  VideoStreamType::Extent coded_size_;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_FFMPEG_FFMPEG_VIDEO_DECODER_H_
