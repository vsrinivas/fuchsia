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

// Verify default values
TEST(AudioClockTest, Defaults) {
  AudioClock audio_clock;
  EXPECT_FALSE(audio_clock);
  EXPECT_FALSE(audio_clock.is_valid());
  EXPECT_FALSE(audio_clock.is_adjustable());
  EXPECT_FALSE(audio_clock.is_device_clock());
  EXPECT_TRUE(audio_clock.is_client_clock());
  EXPECT_FALSE(audio_clock.controls_hardware_clock());
}

TEST(AudioClockTest, ComparisonOperator) {
  auto clock1 = AudioClock::CreateAsOptimal(clock::AdjustableCloneOfMonotonic());
  auto clock2 = AudioClock::CreateAsOptimal(clock::AdjustableCloneOfMonotonic());
  EXPECT_FALSE(clock1 == clock2);

  auto clock3 = AudioClock::CreateAsCustom(clock1.DuplicateClock());
  EXPECT_TRUE(clock1 == clock3);
}

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

// Clock type (and domain, for Device clocks) can be set after construction, and chained.
TEST(AudioClockTest, CreateAs) {
  AudioClock audio_clock;
  audio_clock = AudioClock::CreateAsOptimal(clock::AdjustableCloneOfMonotonic());
  EXPECT_TRUE(audio_clock.is_adjustable());
  EXPECT_FALSE(audio_clock.is_device_clock());
  EXPECT_TRUE(audio_clock.is_client_clock());
  EXPECT_FALSE(audio_clock.controls_hardware_clock());
  EXPECT_FALSE(audio_clock.SetAsHardwareControlling(true));
  EXPECT_FALSE(audio_clock.controls_hardware_clock());

  audio_clock = AudioClock::CreateAsCustom(clock::AdjustableCloneOfMonotonic());
  EXPECT_FALSE(audio_clock.is_adjustable());
  EXPECT_FALSE(audio_clock.is_device_clock());
  EXPECT_TRUE(audio_clock.is_client_clock());
  EXPECT_FALSE(audio_clock.controls_hardware_clock());
  EXPECT_TRUE(audio_clock.SetAsHardwareControlling(true));
  EXPECT_TRUE(audio_clock.controls_hardware_clock());

  constexpr uint32_t kCustomDomain = 42;
  audio_clock = AudioClock::CreateAsDeviceAdjustable(clock::CloneOfMonotonic(), kCustomDomain);
  EXPECT_TRUE(audio_clock.is_adjustable());
  EXPECT_TRUE(audio_clock.is_device_clock());
  EXPECT_FALSE(audio_clock.is_client_clock());
  EXPECT_EQ(audio_clock.domain(), kCustomDomain);
  EXPECT_FALSE(audio_clock.controls_hardware_clock());
  EXPECT_FALSE(audio_clock.SetAsHardwareControlling(true));
  EXPECT_FALSE(audio_clock.controls_hardware_clock());

  constexpr uint32_t kCustomDomain2 = 68;
  audio_clock = AudioClock::CreateAsDeviceStatic(clock::CloneOfMonotonic(), kCustomDomain2);
  EXPECT_FALSE(audio_clock.is_adjustable());
  EXPECT_TRUE(audio_clock.is_device_clock());
  EXPECT_FALSE(audio_clock.is_client_clock());
  EXPECT_EQ(audio_clock.domain(), kCustomDomain2);
  EXPECT_FALSE(audio_clock.controls_hardware_clock());
  EXPECT_FALSE(audio_clock.SetAsHardwareControlling(true));
  EXPECT_FALSE(audio_clock.controls_hardware_clock());
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
