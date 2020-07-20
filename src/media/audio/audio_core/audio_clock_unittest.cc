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

  auto time1 = audio_clock.Read().get();
  auto time2 = audio_clock.Read().get();

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

  auto dupe_clock = audio_clock.DuplicateClock();
  EXPECT_TRUE(dupe_clock.is_valid());

  auto dupe_audio_clock = AudioClock::CreateAsCustom(std::move(dupe_clock));

  auto time1 = dupe_audio_clock.Read().get();
  auto time2 = dupe_audio_clock.Read().get();

  EXPECT_LT(time1, time2);
}

TEST(AudioClockTest, DuplicateWithInvalidClocks) {
  AudioClock uninitialized_audio_clock;
  auto dupe_uninit_audio_clock = uninitialized_audio_clock.DuplicateClock();
  EXPECT_FALSE(dupe_uninit_audio_clock.is_valid());

  auto has_invalid_zx_clock = AudioClock::CreateAsCustom(zx::clock());
  auto dupe_uninit = has_invalid_zx_clock.DuplicateClock();
  EXPECT_FALSE(dupe_uninit.is_valid());
}

TEST(AudioClockTest, InvalidAudioClockHalts) {
  AudioClock uninitialized_audio_clock;

  ASSERT_DEATH(uninitialized_audio_clock.ref_clock_to_clock_mono(), "");

  ASSERT_DEATH(uninitialized_audio_clock.Read(), "");
  ASSERT_DEATH(uninitialized_audio_clock.ReferenceTimeFromMonotonicTime(zx::time{0}), "");
  ASSERT_DEATH(uninitialized_audio_clock.MonotonicTimeFromReferenceTime(zx::time{0}), "");

  auto dupe_uninit = uninitialized_audio_clock.DuplicateClock();
  EXPECT_FALSE(dupe_uninit.is_valid());
}

TEST(AudioClockTest, InvalidZxClockCausesHalt) {
  auto has_invalid_zx_clock = AudioClock::CreateAsCustom(zx::clock());

  ASSERT_DEATH(has_invalid_zx_clock.ref_clock_to_clock_mono(), "");

  ASSERT_DEATH(has_invalid_zx_clock.Read(), "");
  ASSERT_DEATH(has_invalid_zx_clock.ReferenceTimeFromMonotonicTime(zx::time{0}), "");
  ASSERT_DEATH(has_invalid_zx_clock.MonotonicTimeFromReferenceTime(zx::time{0}), "");

  auto dupe_uninit = has_invalid_zx_clock.DuplicateClock();
  EXPECT_FALSE(dupe_uninit.is_valid());
}

}  // namespace
}  // namespace media::audio
