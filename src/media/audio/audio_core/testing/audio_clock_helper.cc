// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/testing/audio_clock_helper.h"

#include <gtest/gtest.h>

#include "src/media/audio/audio_core/audio_clock.h"
#include "src/media/audio/lib/clock/testing/clock_test.h"

namespace media::audio::audio_clock_helper {

const zx::clock& get_underlying_zx_clock(const AudioClock& audio_clock) {
  return audio_clock.clock_;
}

void VerifyReadOnlyRights(const AudioClock& audio_clock) {
  audio::clock::testing::VerifyReadOnlyRights(get_underlying_zx_clock(audio_clock));
}

void VerifyAdvances(const AudioClock& audio_clock) {
  audio::clock::testing::VerifyAdvances(get_underlying_zx_clock(audio_clock));
}

void VerifyCannotBeRateAdjusted(const AudioClock& audio_clock) {
  audio::clock::testing::VerifyCannotBeRateAdjusted(get_underlying_zx_clock(audio_clock));
}
void VerifyCanBeRateAdjusted(const AudioClock& audio_clock) {
  audio::clock::testing::VerifyCanBeRateAdjusted(get_underlying_zx_clock(audio_clock));
}

void VerifySame(const AudioClock& audio_clock1, const AudioClock& audio_clock2) {
  ASSERT_TRUE(audio_clock1 == audio_clock2);
}
void VerifyNotSame(const AudioClock& audio_clock1, const AudioClock& audio_clock2) {
  ASSERT_FALSE(audio_clock1 == audio_clock2);
}

void VerifyIsSystemMonotonic(const AudioClock& audio_clock) {
  audio::clock::testing::VerifyIsSystemMonotonic(get_underlying_zx_clock(audio_clock));
}
void VerifyIsNotSystemMonotonic(const AudioClock& audio_clock) {
  audio::clock::testing::VerifyIsNotSystemMonotonic(get_underlying_zx_clock(audio_clock));
}

}  // namespace media::audio::audio_clock_helper
