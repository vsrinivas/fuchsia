// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_SW_FFMPEG_CODEC_ADAPTER_FFMPEG_ENCODER_H_
#define GARNET_BIN_MEDIA_CODECS_SW_FFMPEG_CODEC_ADAPTER_FFMPEG_ENCODER_H_

#include "codec_adapter_sw.h"

class CodecAdapterFfmpegEncoder : public CodecAdapterSW {
 public:
  CodecAdapterFfmpegEncoder(std::mutex& lock,
                            CodecAdapterEvents* codec_adapter_events);
  ~CodecAdapterFfmpegEncoder();

  void CoreCodecAddBuffer(CodecPort port, const CodecBuffer* buffer) override;

 protected:
  // Processes input in a loop. Should only execute on input_processing_thread_.
  // Loops for the lifetime of a stream.
  void ProcessInputLoop() override;

  void UnreferenceOutputPacket(CodecPacket* packet) override;

  void UnreferenceClientBuffers() override;

  void BeginStopInputProcessing() override;

  void CleanUpAfterStream() override;

  std::pair<fuchsia::media::FormatDetails, size_t> OutputFormatDetails()
      override;

 private:
};

#endif  // GARNET_BIN_MEDIA_CODECS_SW_FFMPEG_CODEC_ADAPTER_FFMPEG_ENCODER_H_
