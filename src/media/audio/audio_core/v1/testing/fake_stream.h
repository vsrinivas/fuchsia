// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_TESTING_FAKE_STREAM_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_TESTING_FAKE_STREAM_H_

#include <lib/zx/clock.h>

#include "src/media/audio/audio_core/v1/clock.h"
#include "src/media/audio/audio_core/v1/stream.h"
#include "src/media/audio/audio_core/v1/versioned_timeline_function.h"
#include "src/media/audio/lib/clock/clone_mono.h"
#include "src/media/audio/lib/processing/gain.h"

namespace media::audio::testing {

class FakeStream : public ReadableStream {
 public:
  FakeStream(const Format& format, std::shared_ptr<AudioCoreClockFactory> clock_factory,
             size_t max_buffer_size = 0, zx::clock clock = audio::clock::CloneOfMonotonic());

  void set_usage_mask(StreamUsageMask mask) { usage_mask_ = mask; }
  void set_gain_db(float gain_db) { gain_db_ = gain_db; }
  void set_max_frame(int64_t max_frame) { max_frame_ = max_frame; }

  const fbl::RefPtr<VersionedTimelineFunction>& timeline_function() const {
    return timeline_function_;
  }

  // |media::audio::ReadableStream|
  TimelineFunctionSnapshot ref_time_to_frac_presentation_frame() const override;
  std::shared_ptr<Clock> reference_clock() override { return audio_clock_; }

 private:
  std::optional<Buffer> ReadLockImpl(ReadLockContext& ctx, Fixed frame,
                                     int64_t frame_count) override;
  void TrimImpl(Fixed frame) override {}

  fbl::RefPtr<VersionedTimelineFunction> timeline_function_ =
      fbl::MakeRefCounted<VersionedTimelineFunction>();
  size_t buffer_size_;
  StreamUsageMask usage_mask_;
  float gain_db_ = media_audio::kUnityGainDb;
  int64_t max_frame_ = std::numeric_limits<int64_t>::max();
  std::unique_ptr<uint8_t[]> buffer_;

  std::shared_ptr<Clock> audio_clock_;
};

}  // namespace media::audio::testing

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_TESTING_FAKE_STREAM_H_
