// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_SW_FFMPEG_CODEC_ADAPTER_FFMPEG_DECODER_H_
#define GARNET_BIN_MEDIA_CODECS_SW_FFMPEG_CODEC_ADAPTER_FFMPEG_DECODER_H_

#include <codec_adapter_sw.h>

#include "avcodec_context.h"
#include "buffer_pool.h"

class CodecAdapterFfmpegDecoder : public CodecAdapterSW {
 public:
  CodecAdapterFfmpegDecoder(std::mutex& lock,
                            CodecAdapterEvents* codec_adapter_events);
  ~CodecAdapterFfmpegDecoder();

  void CoreCodecAddBuffer(CodecPort port, const CodecBuffer* buffer) override;

 protected:
  // Processes input in a loop. Should only execute on input_processing_thread_.
  // Loops for the lifetime of a stream.
  void ProcessInputLoop() override;

  void UnreferenceOutputPacket(CodecPacket* packet) override;

  void UnreferenceClientBuffers() override;

  std::pair<fuchsia::media::FormatDetails, size_t> OutputFormatDetails()
      override;

  void BeginStopInputProcessing() override;

  void CleanUpAfterStream() override;

 private:
  // Allocates buffer for a frame for ffmpeg.
  int GetBuffer(const BufferPool::FrameBufferRequest& decoded_output_info,
                AVCodecContext* avcodec_context, AVFrame* frame, int flags);

  // Decodes frames until the decoder is empty.
  void DecodeFrames();

  std::optional<BufferPool::FrameBufferRequest> decoded_output_info_
      FXL_GUARDED_BY(lock_);
  // This keeps buffers alive via the decoder's refcount until the client is
  // done with them.
  std::map<CodecPacket*, AvCodecContext::AVFramePtr> in_use_by_client_
      FXL_GUARDED_BY(lock_);

  BufferPool output_buffer_pool_;
  std::unique_ptr<AvCodecContext> avcodec_context_;
};

#endif  // GARNET_BIN_MEDIA_CODECS_SW_FFMPEG_CODEC_ADAPTER_FFMPEG_DECODER_H_
