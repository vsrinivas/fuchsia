// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_PLAYER_TEST_FAKE_DECODER_H_
#define GARNET_BIN_MEDIA_PLAYER_TEST_FAKE_DECODER_H_

#include "garnet/bin/media/decode/decoder.h"

namespace media_player {
namespace test {

class FakeDecoder : public Decoder {
 public:
  static std::unique_ptr<media::StreamType> OutputStreamType(
      const media::StreamType& stream_type);

  FakeDecoder(const media::StreamType& stream_type)
      : output_stream_type_(OutputStreamType(stream_type)) {}

  ~FakeDecoder() override {}

  const char* label() const override { return "FakeDecoder"; }

  // Decoder implementation.
  void Flush() override {}

  bool TransformPacket(
      const media::PacketPtr& input,
      bool new_input,
      const std::shared_ptr<media::PayloadAllocator>& allocator,
      media::PacketPtr* output) override {
    return true;
  }

  std::unique_ptr<media::StreamType> output_stream_type() override {
    FXL_DCHECK(output_stream_type_);
    return output_stream_type_->Clone();
  }

 private:
  std::unique_ptr<media::StreamType> output_stream_type_;
};

}  // namespace test
}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_PLAYER_TEST_FAKE_DECODER_H_
