// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/services/framework_ffmpeg/ffmpeg_decoder.h"

#include "apps/media/services/framework_ffmpeg/av_codec_context.h"
#include "apps/media/services/framework_ffmpeg/ffmpeg_audio_decoder.h"
#include "apps/media/services/framework_ffmpeg/ffmpeg_video_decoder.h"

namespace mojo {
namespace media {

Result FfmpegDecoder::Create(const StreamType& stream_type,
                             std::shared_ptr<Decoder>* decoder_out) {
  FTL_DCHECK(decoder_out);

  AvCodecContextPtr av_codec_context(AvCodecContext::Create(stream_type));
  if (!av_codec_context) {
    FTL_LOG(ERROR) << "couldn't create codec context";
    return Result::kUnsupportedOperation;
  }

  AVCodec* ffmpeg_decoder = avcodec_find_decoder(av_codec_context->codec_id);
  if (ffmpeg_decoder == nullptr) {
    FTL_LOG(ERROR) << "couldn't find decoder context";
    return Result::kUnsupportedOperation;
  }

  int r = avcodec_open2(av_codec_context.get(), ffmpeg_decoder, nullptr);
  if (r < 0) {
    FTL_LOG(ERROR) << "couldn't open the decoder " << r;
    return Result::kUnknownError;
  }

  switch (av_codec_context->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
      *decoder_out = std::shared_ptr<Decoder>(
          new FfmpegAudioDecoder(std::move(av_codec_context)));
      break;
    case AVMEDIA_TYPE_VIDEO:
      *decoder_out = std::shared_ptr<Decoder>(
          new FfmpegVideoDecoder(std::move(av_codec_context)));
      break;
    default:
      FTL_LOG(ERROR) << "unsupported codec type "
                     << av_codec_context->codec_type;
      return Result::kUnsupportedOperation;
  }

  return Result::kOk;
}

}  // namespace media
}  // namespace mojo
