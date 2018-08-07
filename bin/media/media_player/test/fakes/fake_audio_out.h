// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_TEST_FAKES_FAKE_AUDIO_OUT_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_TEST_FAKES_FAKE_AUDIO_OUT_H_

#include <memory>
#include <queue>
#include <vector>

#include <fuchsia/media/cpp/fidl.h>
#include <lib/async/dispatcher.h>

#include "garnet/bin/media/media_player/test/fakes/packet_info.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/media/timeline/timeline_function.h"
#include "lib/media/transport/mapped_shared_buffer.h"

namespace media_player {
namespace test {

// Implements AudioOut for testing.
class FakeAudioOut : public fuchsia::media::AudioOut,
                     public fuchsia::media::GainControl {
 public:
  FakeAudioOut();

  ~FakeAudioOut() override;

  // Binds the renderer.
  void Bind(fidl::InterfaceRequest<fuchsia::media::AudioOut> request);

  // Indicates that the renderer should print out supplied packet info.
  void DumpPackets() { dump_packets_ = true; }

  // Indicates that the renderer should verify supplied packets against the
  // indicated PacketInfos.
  void ExpectPackets(const std::vector<PacketInfo>&& expected_packets_info) {
    expected_packets_info_ = std::move(expected_packets_info);
    expected_packets_info_iter_ = expected_packets_info_.begin();
  }

  // Returns true if everything has gone as expected so far.
  bool expected() { return expected_; }

  // AudioRenderer implementation.
  void SetPcmStreamType(fuchsia::media::AudioStreamType format) override;

  void SetStreamType(fuchsia::media::StreamType format) override;

  void AddPayloadBuffer(uint32_t id, ::zx::vmo payload_buffer) override;

  void RemovePayloadBuffer(uint32_t id) override;

  void SetPtsUnits(uint32_t tick_per_second_numerator,
                   uint32_t tick_per_second_denominator) override;

  void SetPtsContinuityThreshold(float threshold_seconds) override;

  void SetReferenceClock(::zx::handle ref_clock) override;

  void SendPacket(fuchsia::media::StreamPacket packet,
                  SendPacketCallback callback) override;

  void SendPacketNoReply(fuchsia::media::StreamPacket packet) override;

  void EndOfStream() final;

  void DiscardAllPackets(DiscardAllPacketsCallback callback) override;

  void DiscardAllPacketsNoReply() override;

  void Play(int64_t reference_time, int64_t media_time,
            PlayCallback callback) override;

  void PlayNoReply(int64_t reference_time, int64_t media_time) override;

  void Pause(PauseCallback callback) override;

  void PauseNoReply() override;

  void BindGainControl(
      ::fidl::InterfaceRequest<fuchsia::media::GainControl> request) override;

  void EnableMinLeadTimeEvents(bool enabled) override;

  void GetMinLeadTime(GetMinLeadTimeCallback callback) override;

  // GainControl interface.
  void SetGain(float gain_db) override;

  void SetMute(bool muted) override;

 private:
  // Converts a pts in |pts_rate_| units to ns.
  int64_t to_ns(int64_t pts) {
    return pts * (media::TimelineRate::NsPerSecond / pts_rate_);
  }

  // Converts a pts in ns to |pts_rate_| units.
  int64_t from_ns(int64_t pts) {
    return pts * (pts_rate_ / media::TimelineRate::NsPerSecond);
  }

  // Determines if we care currently playing.
  bool progressing() { return timeline_function_.invertable(); }

  // Schedules the retirement of the oldest queued packet if there are any
  // packets and if we're playing.
  void MaybeScheduleRetirement();

  async_dispatcher_t* dispatcher_;
  fidl::Binding<fuchsia::media::AudioOut> binding_;
  fidl::BindingSet<fuchsia::media::GainControl> gain_control_bindings_;

  fuchsia::media::AudioStreamType format_;
  media::MappedSharedBuffer mapped_buffer_;
  float threshold_seconds_ = 0.0f;
  float gain_ = 1.0f;
  bool mute_ = false;
  const int64_t min_lead_time_ns_ = ZX_MSEC(100);
  media::TimelineRate pts_rate_ = media::TimelineRate::NsPerSecond;
  media::TimelineFunction timeline_function_;
  int64_t restart_media_time_ = fuchsia::media::NO_TIMESTAMP;

  bool dump_packets_ = false;
  std::vector<PacketInfo> expected_packets_info_;
  std::vector<PacketInfo>::iterator expected_packets_info_iter_;

  std::queue<std::pair<fuchsia::media::StreamPacket, SendPacketCallback>>
      packet_queue_;

  bool expected_ = true;
};

}  // namespace test
}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_TEST_FAKES_FAKE_AUDIO_OUT_H_
