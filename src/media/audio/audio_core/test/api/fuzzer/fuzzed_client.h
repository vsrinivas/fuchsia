// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_TEST_API_FUZZER_FUZZED_CLIENT_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_TEST_API_FUZZER_FUZZED_CLIENT_H_

#include <queue>

#include <fuzzer/FuzzedDataProvider.h>

#include "src/media/audio/lib/test/capturer_shim.h"
#include "src/media/audio/lib/test/renderer_shim.h"

namespace media::audio::test {

using ASF = fuchsia::media::AudioSampleFormat;

struct FuzzerConst {
  static const uint32_t kFrameRate = 48000;
  static const ASF SampleFormat = ASF::FLOAT;
};

template <typename T>
class FuzzedClient {
 public:
  FuzzedClient(T* client, FuzzedDataProvider& data) : client_(client), data_(data) {}
  virtual ~FuzzedClient() = default;
  virtual fit::function<bool()> Step() = 0;
  virtual void Random() = 0;

  void Unbind() {
    std::for_each(gain_controls_.begin(), gain_controls_.end(), [](auto& gc) { gc.Unbind(); });
  }

  void BindGainControl() {
    fuchsia::media::audio::GainControlPtr gc;
    if (gain_controls_.size() < kMaxGainControls) {
      client_->fidl()->BindGainControl(gc.NewRequest());
      gain_controls_.push_back(std::move(gc));
    }
  }

  T* client() { return client_; }
  FuzzedDataProvider& data() { return data_; }

  void set_end_of_stream(bool end) { end_of_stream_ = end; }
  bool Done() { return end_of_stream_; }

 private:
  static const uint32_t kMaxGainControls = 3;

  T* client_ = nullptr;
  FuzzedDataProvider& data_;
  bool end_of_stream_ = false;
  std::vector<fuchsia::media::audio::GainControlPtr> gain_controls_;
};

class FuzzedCapturer : public FuzzedClient<AudioCapturerShim<FuzzerConst::SampleFormat>> {
 public:
  FuzzedCapturer(AudioCapturerShim<FuzzerConst::SampleFormat>* capturer,
                 FuzzedDataProvider& data_provider)
      : FuzzedClient(capturer, data_provider) {}

  // |FuzzedClient|
  fit::function<bool()> Step() final;
  void Random() final;

 private:
  static constexpr size_t kPacketFrames =
      FuzzerConst::kFrameRate / 4000 * RendererShimImpl::kPacketMs;
  enum CaptureStep {
    kDiscardAllPackets = 0,  // Must be first as a default to end loop.
    kDiscardAllPacketsNoReply,
    kCaptureAt,
    kOnPacketProduced,
    kStopAsyncCapture,
    kStopAsyncCaptureNoReply,
    kRandom,
    kMaxValue = kRandom,
  };

  bool async_capture_active_ = false;
  bool sync_capture_active_ = false;
  std::queue<fuchsia::media::StreamPacket> captured_packets_;
};

class FuzzedRenderer : public FuzzedClient<AudioRendererShim<FuzzerConst::SampleFormat>> {
 public:
  FuzzedRenderer(AudioRendererShim<FuzzerConst::SampleFormat>* renderer,
                 FuzzedDataProvider& data_provider)
      : FuzzedClient(renderer, data_provider) {
    // Initialize and send first packet, then Play.
    packet_ = fuchsia::media::StreamPacket{
        .payload_buffer_id = 0, .payload_offset = 0, .payload_size = sizeof(float) * 2};

    bool reply = data().ConsumeBool();
    reply ? client()->fidl()->SendPacket(fidl::Clone(packet_), []() {})
          : client()->fidl()->SendPacketNoReply(fidl::Clone(packet_));

    reply = data().ConsumeBool();
    reply
        ? client()->fidl()->Play(fuchsia::media::NO_TIMESTAMP, fuchsia::media::NO_TIMESTAMP,
                                 [](int64_t, int64_t) {})
        : client()->fidl()->PlayNoReply(fuchsia::media::NO_TIMESTAMP, fuchsia::media::NO_TIMESTAMP);
  }

  // |FuzzedClient|
  fit::function<bool()> Step() final;
  void Random() final;

 private:
  static const int32_t kMaxPackets = 400;

  enum RenderStep {
    kDiscardAllPackets = 0,  // Must be first as a default to end loop.
    kDiscardAllPacketsNoReply,
    kSendPacket,
    kSendPacketNoReply,
    kPause,
    kPauseNoReply,
    kRandom,
    kMaxValue = kRandom,
  };

  // Start count at 1 to account for the packet sent during Init().
  int sent_packets_ = 1;
  fuchsia::media::StreamPacket packet_;
};

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_TEST_API_FUZZER_FUZZED_CLIENT_H_
