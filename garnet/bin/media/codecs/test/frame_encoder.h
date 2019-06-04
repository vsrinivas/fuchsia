// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_TEST_FRAME_ENCODER_H_
#define GARNET_BIN_MEDIA_CODECS_TEST_FRAME_ENCODER_H_

#include <lib/component/cpp/startup_context.h>
#include <lib/media/test/codec_client.h>

#include <vector>

class FrameEncoder {
 public:
  struct EncodedFrame {
    std::optional<uint64_t> timestamp_ish;
    std::vector<uint8_t> data;
  };

  // Payload offsets are indices into a payload that mark where individual
  // packets should be sent and the timestamp value for their packet.
  struct PayloadOffset {
    size_t position;
    std::optional<uint64_t> timestamp_ish;
  };

  // Payload is the data to send to the decoder.
  struct Payload {
    std::vector<uint8_t> data;
    // A set of offsets to mark packets. Each packet is implicitly sized by the
    // distance from an offset to the next offset, or the end of the payload
    // data if is the last offset.
    std::vector<PayloadOffset> offsets;
  };

  // Encodes `payload` using the codec vended by CodecFactory for the
  // `input_details`. If `expect_access_units` is true, each packet will be
  // expected to be on access unit boundaries.
  static std::vector<EncodedFrame> EncodeFrames(
      const Payload& payload,
      const fuchsia::media::FormatDetails& input_details,
      component::StartupContext* startup_context, bool expect_access_units);
};

#endif  // GARNET_BIN_MEDIA_CODECS_TEST_FRAME_ENCODER_H_
