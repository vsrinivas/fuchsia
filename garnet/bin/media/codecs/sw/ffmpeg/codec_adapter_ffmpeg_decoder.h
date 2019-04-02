// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_SW_FFMPEG_CODEC_ADAPTER_FFMPEG_DECODER_H_
#define GARNET_BIN_MEDIA_CODECS_SW_FFMPEG_CODEC_ADAPTER_FFMPEG_DECODER_H_

#include <codec_adapter_sw.h>

#include "avcodec_context.h"
#include "buffer_pool.h"

class CodecAdapterFfmpegDecoder
    : public CodecAdapterSW<AvCodecContext::AVFramePtr> {
 public:
  CodecAdapterFfmpegDecoder(std::mutex& lock,
                            CodecAdapterEvents* codec_adapter_events);
  ~CodecAdapterFfmpegDecoder();

 protected:
  // Processes input in a loop. Should only execute on input_processing_thread_.
  // Loops for the lifetime of a stream.
  void ProcessInputLoop() override;

  std::pair<fuchsia::media::FormatDetails, size_t> OutputFormatDetails()
      override;

  void CleanUpAfterStream() override;

 private:
  static void FfmpegFreeBufferCallback(void* ctx, uint8_t* base);

  // Allocates buffer for a frame for ffmpeg.
  int GetBuffer(const AvCodecContext::FrameBufferRequest& decoded_output_info,
                AVCodecContext* avcodec_context, AVFrame* frame, int flags);

  // Decodes frames until the decoder is empty.
  void DecodeFrames();

  std::optional<AvCodecContext::FrameBufferRequest> decoded_output_info_
      FXL_GUARDED_BY(lock_);

  std::unique_ptr<AvCodecContext> avcodec_context_;
};

#endif  // GARNET_BIN_MEDIA_CODECS_SW_FFMPEG_CODEC_ADAPTER_FFMPEG_DECODER_H_
