// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_TEST_FAKE_RENDERER_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_TEST_FAKE_RENDERER_H_

#include <memory>
#include <queue>
#include <vector>

#include <media/cpp/fidl.h>

#include "lib/fidl/cpp/binding.h"
#include "lib/media/transport/mapped_shared_buffer.h"

namespace media_player {

// Implements AudioRenderer2 for testing.
class FakeAudioRenderer : public media::AudioRenderer2 {
 public:
  class PacketInfo {
   public:
    PacketInfo(int64_t timestamp, uint64_t size, uint64_t hash)
        : timestamp_(timestamp), size_(size), hash_(hash) {}

    int64_t timestamp() const { return timestamp_; }
    uint64_t size() const { return size_; }
    uint64_t hash() const { return hash_; }

   private:
    int64_t timestamp_;
    uint64_t size_;
    uint64_t hash_;
  };

  FakeAudioRenderer();

  ~FakeAudioRenderer() override;

  // Binds the renderer.
  void Bind(fidl::InterfaceRequest<media::AudioRenderer2> request);

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

  // AudioRenderer2 implementation.
  void SetPcmFormat(media::AudioPcmFormat format) override;

  void SetPayloadBuffer(::zx::vmo payload_buffer) override;

  void SetPtsUnits(uint32_t tick_per_second_numerator,
                   uint32_t tick_per_second_denominator) override;

  void SetPtsContinuityThreshold(float threshold_seconds) override;

  void SetReferenceClock(::zx::handle ref_clock) override;

  void SendPacket(media::AudioPacket packet,
                  SendPacketCallback callback) override;

  void SendPacketNoReply(media::AudioPacket packet) override;

  void Flush(FlushCallback callback) override;

  void FlushNoReply() override;

  void Play(int64_t reference_time,
            int64_t media_time,
            PlayCallback callback) override;

  void PlayNoReply(int64_t reference_time, int64_t media_time) override;

  void Pause(PauseCallback callback) override;

  void PauseNoReply() override;

  void SetGainMute(float gain,
                   bool mute,
                   uint32_t flags,
                   SetGainMuteCallback callback) override;

  void SetGainMuteNoReply(float gain, bool mute, uint32_t flags) override;

  void DuplicateGainControlInterface(
      ::fidl::InterfaceRequest<media::AudioRendererGainControl> request)
      override;

  void EnableMinLeadTimeEvents(bool enabled) override;

  void GetMinLeadTime(GetMinLeadTimeCallback callback) override;

 private:
  fidl::Binding<media::AudioRenderer2> binding_;

  media::AudioPcmFormat format_;
  media::MappedSharedBuffer mapped_buffer_;
  uint32_t tick_per_second_numerator_ = 1'000'000'000;
  uint32_t tick_per_second_denominator_ = 1;
  float threshold_seconds_ = 0.0f;
  float gain_ = 1.0f;
  bool mute_ = false;
  uint32_t gain_mute_flags_ = 0;
  const int64_t min_lead_time_ns_ = 100'000'000;
  bool playing_ = false;

  bool dump_packets_ = false;
  std::vector<PacketInfo> expected_packets_info_;
  std::vector<PacketInfo>::iterator expected_packets_info_iter_;

  std::queue<SendPacketCallback> packet_callback_queue_;

  bool expected_ = true;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_TEST_FAKE_RENDERER_H_
