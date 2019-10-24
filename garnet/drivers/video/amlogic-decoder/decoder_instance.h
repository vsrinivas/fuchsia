// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_DECODER_INSTANCE_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_DECODER_INSTANCE_H_

#include "decoder_core.h"
#include "stream_buffer.h"
#include "video_decoder.h"

class DecoderInstance {
 public:
  DecoderInstance(std::unique_ptr<VideoDecoder> decoder, DecoderCore* core)
      : stream_buffer_(std::make_unique<StreamBuffer>()),
        decoder_(std::move(decoder)),
        core_(core) {}

  StreamBuffer* stream_buffer() const { return stream_buffer_.get(); }
  VideoDecoder* decoder() const { return decoder_.get(); }
  InputContext* input_context() const { return input_context_.get(); }
  DecoderCore* core() const { return core_; }

  void InitializeInputContext() {
    assert(!input_context_);
    input_context_ = std::make_unique<InputContext>();
  }

 private:
  std::unique_ptr<StreamBuffer> stream_buffer_;
  // The decoder must be destroyed before the stream buffer, to ensure it's not
  // running and decoding from the buffer.
  std::unique_ptr<VideoDecoder> decoder_;
  std::unique_ptr<InputContext> input_context_;
  DecoderCore* core_ = nullptr;
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_DECODER_INSTANCE_H_
