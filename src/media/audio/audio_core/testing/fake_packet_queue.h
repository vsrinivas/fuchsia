// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_FAKE_PACKET_QUEUE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_FAKE_PACKET_QUEUE_H_

#include "src/media/audio/audio_core/mixer/gain.h"
#include "src/media/audio/audio_core/stream.h"
#include "src/media/audio/audio_core/versioned_timeline_function.h"
#include "src/media/audio/lib/clock/audio_clock.h"
#include "src/media/audio/lib/clock/clone_mono.h"

namespace media::audio::testing {

class FakePacketQueue : public ReadableStream {
 public:
  // Packets must be sorted by frame.
  FakePacketQueue(std::vector<fbl::RefPtr<Packet>> packets, const Format& format,
                  fbl::RefPtr<VersionedTimelineFunction> ref_time_to_frac_presentation_frame,
                  std::unique_ptr<AudioClock> audio_clock);

  void set_usage_mask(StreamUsageMask mask) { usage_mask_ = mask; }
  void set_gain_db(float gain_db) { gain_db_ = gain_db; }

  const fbl::RefPtr<VersionedTimelineFunction>& timeline_function() const {
    return timeline_function_;
  }

  void PushPacket(fbl::RefPtr<Packet> packet) { packets_.push_back(std::move(packet)); }

  // |media::audio::ReadableStream|
  TimelineFunctionSnapshot ref_time_to_frac_presentation_frame() const override;
  AudioClock& reference_clock() override { return *audio_clock_; }
  std::optional<Buffer> ReadLock(ReadLockContext& ctx, Fixed frame, int64_t frame_count) override;
  void Trim(Fixed frame) override;

 private:
  std::vector<fbl::RefPtr<Packet>> packets_;
  fbl::RefPtr<VersionedTimelineFunction> timeline_function_;
  std::unique_ptr<AudioClock> audio_clock_;
  StreamUsageMask usage_mask_;
  float gain_db_ = Gain::kUnityGainDb;
};

}  // namespace media::audio::testing

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_FAKE_PACKET_QUEUE_H_
