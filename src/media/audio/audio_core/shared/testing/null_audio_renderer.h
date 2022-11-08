// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_SHARED_TESTING_NULL_AUDIO_RENDERER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_SHARED_TESTING_NULL_AUDIO_RENDERER_H_

#include <fuchsia/media/cpp/fidl.h>

namespace media::audio::test {

// A |fuchsia::media::AudioRenderer| that simply does nothing.
class NullAudioRenderer : public fuchsia::media::AudioRenderer {
 private:
  void AddPayloadBuffer(uint32_t id, ::zx::vmo payload_buffer) override {}
  void RemovePayloadBuffer(uint32_t id) override {}
  void SendPacket(fuchsia::media::StreamPacket packet, SendPacketCallback callback) override {}
  void SendPacketNoReply(fuchsia::media::StreamPacket packet) override {}
  void EndOfStream() override {}
  void DiscardAllPackets(DiscardAllPacketsCallback callback) override {}
  void DiscardAllPacketsNoReply() override {}
  void SetPcmStreamType(fuchsia::media::AudioStreamType type) override {}
  void SetPtsUnits(uint32_t tick_per_second_numerator,
                   uint32_t tick_per_second_denominator) override {}
  void SetPtsContinuityThreshold(float threshold_seconds) override {}
  void GetReferenceClock(GetReferenceClockCallback callback) override {}
  void SetReferenceClock(::zx::clock reference_clock) override {}
  void Play(int64_t reference_time, int64_t media_time, PlayCallback callback) override {}
  void PlayNoReply(int64_t reference_time, int64_t media_time) override {}
  void Pause(PauseCallback callback) override {}
  void PauseNoReply() override {}
  void EnableMinLeadTimeEvents(bool enabled) override {}
  void GetMinLeadTime(GetMinLeadTimeCallback callback) override {}
  void BindGainControl(::fidl::InterfaceRequest<::fuchsia::media::audio::GainControl>
                           gain_control_request) override {}
  void SetUsage(fuchsia::media::AudioRenderUsage usage) override {}
};

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_SHARED_TESTING_NULL_AUDIO_RENDERER_H_
