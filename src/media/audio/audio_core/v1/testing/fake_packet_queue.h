// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_TESTING_FAKE_PACKET_QUEUE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_TESTING_FAKE_PACKET_QUEUE_H_

#include "src/media/audio/audio_core/v1/clock.h"
#include "src/media/audio/audio_core/v1/stream.h"
#include "src/media/audio/audio_core/v1/versioned_timeline_function.h"
#include "src/media/audio/lib/clock/clone_mono.h"
#include "src/media/audio/lib/processing/gain.h"

namespace media::audio::testing {

class FakePacketQueue : public ReadableStream {
 public:
  // Packets must be sorted by frame.
  FakePacketQueue(std::vector<fbl::RefPtr<Packet>> packets, const Format& format,
                  fbl::RefPtr<VersionedTimelineFunction> ref_time_to_frac_presentation_frame,
                  std::shared_ptr<Clock> audio_clock);

  void set_usage_mask(StreamUsageMask mask) { usage_mask_ = mask; }
  void set_gain_db(float gain_db) { gain_db_ = gain_db; }

  const fbl::RefPtr<VersionedTimelineFunction>& timeline_function() const {
    return timeline_function_;
  }

  void PushPacket(fbl::RefPtr<Packet> packet) { packets_.push_back(std::move(packet)); }

  // |media::audio::ReadableStream|
  TimelineFunctionSnapshot ref_time_to_frac_presentation_frame() const override;
  std::shared_ptr<Clock> reference_clock() override { return audio_clock_; }

 private:
  // |media::audio::ReadableStream|
  std::optional<Buffer> ReadLockImpl(ReadLockContext& ctx, Fixed frame,
                                     int64_t frame_count) override;
  void TrimImpl(Fixed frame) override;

  std::vector<fbl::RefPtr<Packet>> packets_;
  fbl::RefPtr<VersionedTimelineFunction> timeline_function_;
  std::shared_ptr<Clock> audio_clock_;
  StreamUsageMask usage_mask_;
  float gain_db_ = media_audio::kUnityGainDb;
};

}  // namespace media::audio::testing

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_TESTING_FAKE_PACKET_QUEUE_H_
