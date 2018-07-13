// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_PLAYER_TEST_FAKE_DECODER_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_PLAYER_TEST_FAKE_DECODER_H_

#include "garnet/bin/media/media_player/decode/decoder.h"

namespace media_player {
namespace test {

class FakeDecoder : public Decoder {
 public:
  static std::unique_ptr<StreamType> OutputStreamType(
      const StreamType& stream_type);

  FakeDecoder(const StreamType& stream_type)
      : output_stream_type_(OutputStreamType(stream_type)) {}

  ~FakeDecoder() override {}

  const char* label() const override { return "FakeDecoder"; }

  // Decoder implementation.
  void GetConfiguration(size_t* input_count, size_t* output_count) override {
    FXL_DCHECK(input_count);
    FXL_DCHECK(output_count);
    *input_count = 1;
    *output_count = 1;
  }

  void FlushInput(bool hold_frame, size_t input_index,
                  fit::closure callback) override {
    callback();
  }

  void FlushOutput(size_t output_index, fit::closure callback) override {
    callback();
  }

  std::shared_ptr<PayloadAllocator> allocator_for_input(
      size_t input_index) override {
    return nullptr;
  }

  void PutInputPacket(PacketPtr packet, size_t input_index) override {
    stage()->RequestInputPacket();
  }

  bool can_accept_allocator_for_output(size_t output_index) const override {
    return false;
  }

  void SetAllocatorForOutput(std::shared_ptr<PayloadAllocator> allocator,
                             size_t output_index) override {}

  void RequestOutputPacket() override {}

  std::unique_ptr<StreamType> output_stream_type() const override {
    FXL_DCHECK(output_stream_type_);
    return output_stream_type_->Clone();
  }

 private:
  std::unique_ptr<StreamType> output_stream_type_;
};

class FakeDecoderFactory : public DecoderFactory {
 public:
  FakeDecoderFactory();

  ~FakeDecoderFactory() override;

  void CreateDecoder(
      const StreamType& stream_type,
      fit::function<void(std::shared_ptr<Decoder>)> callback) override;
};

}  // namespace test
}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_PLAYER_TEST_FAKE_DECODER_H_
