// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_SOUNDS_SOUNDPLAYER_TEST_FAKE_AUDIO_RENDERER_H_
#define SRC_MEDIA_SOUNDS_SOUNDPLAYER_TEST_FAKE_AUDIO_RENDERER_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include "lib/fidl/cpp/binding.h"

namespace soundplayer::test {

// Implements AudioRenderer for testing.
class FakeAudioRenderer : public fuchsia::media::AudioRenderer {
 public:
  struct Expectations {
    zx_koid_t payload_buffer_ = ZX_KOID_INVALID;
    std::vector<fuchsia::media::StreamPacket> packets_;
    fuchsia::media::AudioStreamType stream_type_;
    fuchsia::media::AudioRenderUsage usage_;
    bool block_completion_ = false;
  };

  FakeAudioRenderer();

  ~FakeAudioRenderer() override;

  // Sets expectations.
  void SetExpectations(const Expectations& expectations) {
    expectations_ = expectations;
    expected_packets_iterator_ = expectations_.packets_.begin();
  }

  bool completed() const { return play_no_reply_called_; }

  // Binds the renderer.
  void Bind(fidl::InterfaceRequest<fuchsia::media::AudioRenderer> request,
            fit::function<void(zx_status_t)> error_handler);

  // AudioRenderer implementation.
  void SetPcmStreamType(fuchsia::media::AudioStreamType format) override;

  void AddPayloadBuffer(uint32_t id, zx::vmo payload_buffer) override;

  void RemovePayloadBuffer(uint32_t id) override;

  void SetPtsUnits(uint32_t tick_per_second_numerator,
                   uint32_t tick_per_second_denominator) override;

  void SetPtsContinuityThreshold(float threshold_seconds) override;

  void SetReferenceClock(zx::clock ref_clock) override;

  void GetReferenceClock(GetReferenceClockCallback callback) override;

  void SendPacket(fuchsia::media::StreamPacket packet, SendPacketCallback callback) override;

  void SendPacketNoReply(fuchsia::media::StreamPacket packet) override;

  void EndOfStream() final;

  void DiscardAllPackets(DiscardAllPacketsCallback callback) override;

  void DiscardAllPacketsNoReply() override;

  void Play(int64_t reference_time, int64_t media_time, PlayCallback callback) override;

  void PlayNoReply(int64_t reference_time, int64_t media_time) override;

  void Pause(PauseCallback callback) override;

  void PauseNoReply() override;

  void BindGainControl(fidl::InterfaceRequest<fuchsia::media::audio::GainControl> request) override;

  void EnableMinLeadTimeEvents(bool enabled) override;

  void GetMinLeadTime(GetMinLeadTimeCallback callback) override;

  void SetUsage(fuchsia::media::AudioRenderUsage usage) override;

 private:
  fidl::Binding<fuchsia::media::AudioRenderer> binding_;

  Expectations expectations_;
  std::vector<fuchsia::media::StreamPacket>::const_iterator expected_packets_iterator_;
  SendPacketCallback send_packet_callback_;
  bool set_usage_called_ = false;
  bool set_pcm_stream_type_called_ = false;
  bool add_payload_buffer_called_ = false;
  bool play_no_reply_called_ = false;
};

}  // namespace soundplayer::test

#endif  // SRC_MEDIA_SOUNDS_SOUNDPLAYER_TEST_FAKE_AUDIO_RENDERER_H_
