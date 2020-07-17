// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_clock.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>

#include <gtest/gtest.h>

#include "src/media/audio/lib/clock/clone_mono.h"

namespace media::audio {
namespace {

// Verify operator bool and is_valid(), and being subsequently set
TEST(AudioClockTest, Basic) {
  AudioClock default_audio_clock;
  EXPECT_FALSE(default_audio_clock);
  EXPECT_FALSE(default_audio_clock.is_valid());

  auto audio_clock = AudioClock::CreateAsCustom(clock::CloneOfMonotonic());
  EXPECT_TRUE(audio_clock);
  EXPECT_TRUE(audio_clock.is_valid());

  auto time1 = audio_clock.Read();
  auto time2 = audio_clock.Read();
  EXPECT_LT(time1, time2);
}

TEST(AudioClockTest, ClockMonoToRefClock) {
  auto clock = clock::AdjustableCloneOfMonotonic();

  zx::clock::update_args args;
  args.reset().set_rate_adjust(-1000);
  EXPECT_EQ(clock.update(args), ZX_OK) << "clock.update with rate_adjust failed";

  auto audio_clock = AudioClock::CreateAsCustom(std::move(clock));

  auto post_update_tl_func = audio_clock.ref_clock_to_clock_mono();
  EXPECT_LT(post_update_tl_func.reference_delta(), post_update_tl_func.subject_delta())
      << "rate should be less than 1:1";
}

TEST(AudioClockTest, DuplicateClock) {
  auto audio_clock = AudioClock::CreateAsCustom(clock::CloneOfMonotonic());

  auto result = audio_clock.DuplicateClock();
  EXPECT_FALSE(result.is_error());

  auto dupe_clock = result.take_value();
  EXPECT_TRUE(dupe_clock.is_valid());

  auto dupe_audio_clock = AudioClock::CreateAsCustom(std::move(dupe_clock));
  auto time1 = dupe_audio_clock.Read();
  auto time2 = dupe_audio_clock.Read();
  EXPECT_LT(time1, time2);
}

TEST(AudioClockTest, InvalidClockReturnsErrors) {
  AudioClock uninitialized_audio_clock;
  auto result = uninitialized_audio_clock.ReferenceTimeFromMonotonicTime(zx::time{0});
  EXPECT_TRUE(result.is_error());
  EXPECT_EQ(result.error(), ZX_ERR_BAD_HANDLE);

  result = uninitialized_audio_clock.MonotonicTimeFromReferenceTime(zx::time{0});
  EXPECT_TRUE(result.is_error());
  EXPECT_EQ(result.error(), ZX_ERR_BAD_HANDLE);

  auto dupe_result = uninitialized_audio_clock.DuplicateClock();
  EXPECT_TRUE(dupe_result.is_error());
  EXPECT_EQ(dupe_result.error(), ZX_ERR_BAD_HANDLE);
}

}  // namespace
}  // namespace media::audio
