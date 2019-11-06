// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/fidl/fidl_audio_renderer.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/binding.h"

namespace media_player {
namespace test {

// Fake implementation of |fuchsia::media::StreamProcessor|.
class FakeAudioRenderer : public fuchsia::media::AudioRenderer {
 public:
  FakeAudioRenderer() : binding_(this) {}

  void Bind(fidl::InterfaceRequest<fuchsia::media::AudioRenderer> request) {
    binding_.Bind(std::move(request));
  }

  // fuchsia::media::AudioRenderer implementation.
  void AddPayloadBuffer(uint32_t id, ::zx::vmo payload_buffer) {}
  void RemovePayloadBuffer(uint32_t id) {}
  void SendPacket(fuchsia::media::StreamPacket packet, SendPacketCallback callback) {}
  void SendPacketNoReply(fuchsia::media::StreamPacket packet) {}
  void EndOfStream() {}
  void DiscardAllPackets(DiscardAllPacketsCallback callback) {}
  void DiscardAllPacketsNoReply() {}
  void SetPcmStreamType(fuchsia::media::AudioStreamType type) {}
  void SetPtsUnits(uint32_t tick_per_second_numerator, uint32_t tick_per_second_denominator) {}
  void SetPtsContinuityThreshold(float threshold_seconds) {}
  void SetReferenceClock(::zx::handle reference_clock) {}
  void Play(int64_t reference_time, int64_t media_time, PlayCallback callback) {}
  void PlayNoReply(int64_t reference_time, int64_t media_time) {}
  void Pause(PauseCallback callback) {}
  void PauseNoReply() {}
  void EnableMinLeadTimeEvents(bool enabled) {}
  void GetMinLeadTime(GetMinLeadTimeCallback callback) {}
  void BindGainControl(
      ::fidl::InterfaceRequest<::fuchsia::media::audio::GainControl> gain_control_request) {}
  void SetUsage(fuchsia::media::AudioRenderUsage usage) {}

 private:
  fidl::Binding<fuchsia::media::AudioRenderer> binding_;
};

// Tests that we can destroy the async loop with the AudioRenderer connection in place without
// panicking.
TEST(FidlAudioRendererTest, DestroyLoopWithoutDisconnecting) {
  std::shared_ptr<FidlAudioRenderer> under_test;
  FakeAudioRenderer fake_audio_renderer;

  {
    async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

    fuchsia::media::AudioRendererPtr fake_audio_renderer_ptr;
    fidl::InterfaceRequest<fuchsia::media::AudioRenderer> audio_renderer_request =
        fake_audio_renderer_ptr.NewRequest();
    fake_audio_renderer.Bind(std::move(audio_renderer_request));

    under_test = FidlAudioRenderer::Create(std::move(fake_audio_renderer_ptr));
  }

  // The FidlAudioRenderer still exists at this point and still has a connection to the fake
  // audio renderer. The async loop, however, has gone out of scope.
}

}  // namespace test
}  // namespace media_player
