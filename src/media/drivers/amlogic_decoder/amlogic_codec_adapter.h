// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_AMLOGIC_CODEC_ADAPTER_H_
#define SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_AMLOGIC_CODEC_ADAPTER_H_

#include "amlogic_decoder_test_hooks.h"
#include "lib/media/codec_impl/codec_adapter.h"
#include "video_decoder.h"

namespace amlogic_decoder {

class AmlogicCodecAdapter : public CodecAdapter, public VideoDecoder::Client {
 public:
  AmlogicCodecAdapter(std::mutex& lock, CodecAdapterEvents* codec_adapter_events)
      : CodecAdapter(lock, codec_adapter_events) {}
  void set_test_hooks(AmlogicDecoderTestHooks test_hooks) { test_hooks_ = std::move(test_hooks); }

  // VideoDecoder::Client partial implementation.
  [[nodiscard]] const AmlogicDecoderTestHooks& test_hooks() const override { return test_hooks_; }

 protected:
  AmlogicDecoderTestHooks test_hooks_;
};

}  // namespace amlogic_decoder

#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_AMLOGIC_CODEC_ADAPTER_H_
