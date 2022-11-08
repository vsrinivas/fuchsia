// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_TESTING_FAKE_AUDIO_RENDERER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_TESTING_FAKE_AUDIO_RENDERER_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>

#include <unordered_map>

#include <fbl/ref_ptr.h>

#include "src/media/audio/audio_core/shared/usage_settings.h"
#include "src/media/audio/audio_core/v1/audio_object.h"
#include "src/media/audio/audio_core/v1/clock.h"
#include "src/media/audio/audio_core/v1/link_matrix.h"
#include "src/media/audio/audio_core/v1/packet_queue.h"
#include "src/media/audio/audio_core/v1/testing/packet_factory.h"
#include "src/media/audio/audio_core/v1/utils.h"

namespace media::audio::testing {

class FakeAudioRenderer : public AudioObject, public fuchsia::media::AudioRenderer {
 public:
  static std::shared_ptr<FakeAudioRenderer> Create(
      async_dispatcher_t* dispatcher, std::optional<Format> format,
      fuchsia::media::AudioRenderUsage usage, LinkMatrix* link_matrix,
      std::shared_ptr<AudioCoreClockFactory> clock_factory) {
    return std::make_shared<FakeAudioRenderer>(dispatcher, std::move(format), usage, link_matrix,
                                               clock_factory);
  }
  static std::shared_ptr<FakeAudioRenderer> CreateWithDefaultFormatInfo(
      async_dispatcher_t* dispatcher, LinkMatrix* link_matrix,
      std::shared_ptr<AudioCoreClockFactory> clock_factory);

  FakeAudioRenderer(async_dispatcher_t* dispatcher, std::optional<Format> format,
                    fuchsia::media::AudioRenderUsage usage, LinkMatrix* link_matrix,
                    std::shared_ptr<AudioCoreClockFactory> clock_factory);

  // Enqueues a packet that has all samples initialized to |sample| and lasts for |duration|.
  void EnqueueAudioPacket(float sample, zx::duration duration = zx::msec(1),
                          fit::closure callback = nullptr);

  // |media::audio::AudioObject|
  std::optional<Format> format() const override { return format_; }
  fpromise::result<std::shared_ptr<ReadableStream>, zx_status_t> InitializeDestLink(
      const AudioObject& dest) override;
  void CleanupDestLink(const AudioObject& dest) override;
  std::optional<StreamUsage> usage() const override {
    return {StreamUsage::WithRenderUsage(usage_)};
  }

  // |fuchsia::media::AudioRenderer|
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

 private:
  zx::duration FindMinLeadTime();

  async_dispatcher_t* dispatcher_;
  std::optional<Format> format_;
  fuchsia::media::AudioRenderUsage usage_;
  PacketFactory packet_factory_;
  std::unordered_map<const AudioObject*, std::shared_ptr<PacketQueue>> packet_queues_;
  fbl::RefPtr<VersionedTimelineFunction> timeline_function_ =
      fbl::MakeRefCounted<VersionedTimelineFunction>();
  LinkMatrix& link_matrix_;
  std::shared_ptr<AudioCoreClockFactory> clock_factory_;
};

}  // namespace media::audio::testing

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_TESTING_FAKE_AUDIO_RENDERER_H_
