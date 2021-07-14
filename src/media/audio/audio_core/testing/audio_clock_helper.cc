// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/testing/audio_clock_helper.h"

#include <gtest/gtest.h>

namespace media::audio::audio_clock_helper {

void VerifyReadOnlyRights(const AudioClock& audio_clock) {
  EXPECT_FALSE(audio_clock.is_adjustable());
}

void VerifyAdvances(const AudioClock& audio_clock,
                    std::shared_ptr<AudioClockFactory> clock_factory) {
  constexpr zx::duration kWaitInterval = zx::usec(50);
  zx::time before = audio_clock.Read();
  clock_factory->AdvanceMonoTimeBy(kWaitInterval);
  zx::time after = audio_clock.Read();

  // Due to lack of precision, verify that we have advanced in general, rather than by the specific
  // kWaitInterval amount.
  EXPECT_GE(after - before, kWaitInterval / 2);
}

void VerifyCannotBeRateAdjusted(const AudioClock& audio_clock) {
  EXPECT_FALSE(audio_clock.is_adjustable()) << "AudioClock is adjustable";
}
void VerifyCanBeRateAdjusted(const AudioClock& audio_clock) {
  EXPECT_TRUE(audio_clock.is_adjustable()) << "AudioClock is not adjustable";
}

void VerifySame(const AudioClock& audio_clock1, const AudioClock& audio_clock2) {
  ASSERT_TRUE(audio_clock1 == audio_clock2);
}
void VerifyNotSame(const AudioClock& audio_clock1, const AudioClock& audio_clock2) {
  ASSERT_FALSE(audio_clock1 == audio_clock2);
}

void VerifyIsSystemMonotonic(const AudioClock& audio_clock) {
  EXPECT_EQ(audio_clock.ref_clock_to_clock_mono().subject_time(),
            audio_clock.ref_clock_to_clock_mono().reference_time());
  EXPECT_EQ(audio_clock.ref_clock_to_clock_mono().rate().subject_delta(),
            audio_clock.ref_clock_to_clock_mono().rate().reference_delta());
}
void VerifyIsNotSystemMonotonic(const AudioClock& audio_clock) {
  EXPECT_NE(audio_clock.ref_clock_to_clock_mono().subject_time(),
            audio_clock.ref_clock_to_clock_mono().reference_time());
  EXPECT_NE(audio_clock.ref_clock_to_clock_mono().rate().subject_delta(),
            audio_clock.ref_clock_to_clock_mono().rate().reference_delta());
}

}  // namespace media::audio::audio_clock_helper
