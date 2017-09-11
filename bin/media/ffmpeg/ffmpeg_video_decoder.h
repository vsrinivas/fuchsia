// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/media/ffmpeg/ffmpeg_decoder_base.h"
#include "garnet/bin/media/ffmpeg/ffmpeg_video_frame_layout.h"
#include "lib/media/timeline/timeline_rate.h"

namespace media {

// Decoder implementation employing and ffmpeg video decoder.
// TODO(dalesat): Complete this.
class FfmpegVideoDecoder : public FfmpegDecoderBase {
 public:
  FfmpegVideoDecoder(AvCodecContextPtr av_codec_context);

  ~FfmpegVideoDecoder() override;

 protected:
  // FfmpegDecoderBase overrides.
  void OnNewInputPacket(const PacketPtr& packet) override;

  int BuildAVFrame(const AVCodecContext& av_codec_context,
                   AVFrame* av_frame,
                   PayloadAllocator* allocator) override;

  PacketPtr CreateOutputPacket(
      const AVFrame& av_frame,
      const std::shared_ptr<PayloadAllocator>& allocator) override;

 private:
  FfmpegVideoFrameLayout frame_layout_;
  std::unique_ptr<StreamType> revised_stream_type_;

  // TODO(dalesat): For investigation only...remove these three fields.
  bool first_frame_ = true;
  AVColorSpace colorspace_;
  VideoStreamType::Extent coded_size_;
};

}  // namespace media
