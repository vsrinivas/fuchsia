// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/clock.h>

#include <gtest/gtest.h>

#include "src/media/audio/lib/clock/audio_clock.h"
#include "src/media/audio/lib/clock/clone_mono.h"

static constexpr uint32_t kCustomDomain = 42;

namespace media::audio {

TEST(AudioClockDeathTest, InvalidZxClockHaltsCreate) {
  // Uninitialized clock cannot be passed to CreateAs...
  ASSERT_DEATH(AudioClock::ClientFixed(zx::clock()), "");
  ASSERT_DEATH(AudioClock::ClientAdjustable(zx::clock()), "");
  ASSERT_DEATH(AudioClock::DeviceFixed(zx::clock(), kCustomDomain), "");
  ASSERT_DEATH(AudioClock::DeviceAdjustable(zx::clock(), kCustomDomain), "");

  // Clock without WRITE rights cannot be passed to CreateAs...Adjustable
  ASSERT_DEATH(AudioClock::ClientAdjustable(clock::CloneOfMonotonic()), "");
  ASSERT_DEATH(AudioClock::DeviceAdjustable(clock::CloneOfMonotonic(), kCustomDomain), "");
}

}  // namespace media::audio
