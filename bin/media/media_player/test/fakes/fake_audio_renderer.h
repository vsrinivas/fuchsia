// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_TEST_FAKES_FAKE_AUDIO_RENDERER_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_TEST_FAKES_FAKE_AUDIO_RENDERER_H_

#include <memory>
#include <queue>
#include <vector>

#include <fuchsia/media/cpp/fidl.h>
#include <lib/async/dispatcher.h>

#include "garnet/bin/media/media_player/test/fakes/packet_info.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/media/timeline/timeline_function.h"
#include "lib/media/transport/mapped_shared_buffer.h"

namespace media_player {
namespace test {

// Implements AudioRenderer for testing.
class FakeAudioRenderer : public fuchsia::media::AudioRenderer2 {
 public:
  FakeAudioRenderer();

  ~FakeAudioRenderer() override;

  // Binds the renderer.
  void Bind(fidl::InterfaceRequest<fuchsia::media::AudioRenderer2> request);

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
  void SetPcmFormat(fuchsia::media::AudioPcmFormat format) override;

  void SetPayloadBuffer(::zx::vmo payload_buffer) override;

  void SetPtsUnits(uint32_t tick_per_second_numerator,
                   uint32_t tick_per_second_denominator) override;

  void SetPtsContinuityThreshold(float threshold_seconds) override;

  void SetReferenceClock(::zx::handle ref_clock) override;

  void SendPacket(fuchsia::media::AudioPacket packet,
                  SendPacketCallback callback) override;

  void SendPacketNoReply(fuchsia::media::AudioPacket packet) override;

  void Flush(FlushCallback callback) override;

  void FlushNoReply() override;

  void Play(int64_t reference_time, int64_t media_time,
            PlayCallback callback) override;

  void PlayNoReply(int64_t reference_time, int64_t media_time) override;

  void Pause(PauseCallback callback) override;

  void PauseNoReply() override;

  void SetGainMute(float gain, bool mute, uint32_t flags,
                   SetGainMuteCallback callback) override;

  void SetGainMuteNoReply(float gain, bool mute, uint32_t flags) override;

  void DuplicateGainControlInterface(
      ::fidl::InterfaceRequest<fuchsia::media::AudioRendererGainControl>
          request) override;

  void EnableMinLeadTimeEvents(bool enabled) override;

  void GetMinLeadTime(GetMinLeadTimeCallback callback) override;

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
  fidl::Binding<fuchsia::media::AudioRenderer2> binding_;

  fuchsia::media::AudioPcmFormat format_;
  media::MappedSharedBuffer mapped_buffer_;
  float threshold_seconds_ = 0.0f;
  float gain_ = 1.0f;
  bool mute_ = false;
  uint32_t gain_mute_flags_ = 0;
  const int64_t min_lead_time_ns_ = ZX_MSEC(100);
  media::TimelineRate pts_rate_ = media::TimelineRate::NsPerSecond;
  media::TimelineFunction timeline_function_;
  int64_t restart_media_time_ = fuchsia::media::kNoTimestamp;

  bool dump_packets_ = false;
  std::vector<PacketInfo> expected_packets_info_;
  std::vector<PacketInfo>::iterator expected_packets_info_iter_;

  std::queue<std::pair<fuchsia::media::AudioPacket, SendPacketCallback>>
      packet_queue_;

  bool expected_ = true;
};

}  // namespace test
}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_TEST_FAKES_FAKE_AUDIO_RENDERER_H_
