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
  EXPECT_FALSE(audio_clock.is_device_clock());
  EXPECT_TRUE(audio_clock.is_client_clock());
  EXPECT_FALSE(audio_clock.is_flexible());
  EXPECT_FALSE(audio_clock.is_tuneable());
  EXPECT_FALSE(audio_clock.controls_tuneable_clock());
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

// Clock type (and domain, for Device clocks) can be set after construction.
TEST(AudioClockTest, CreateAs) {
  AudioClock audio_clock;
  audio_clock = AudioClock::CreateAsOptimal(clock::AdjustableCloneOfMonotonic());
  EXPECT_FALSE(audio_clock.is_device_clock());
  EXPECT_TRUE(audio_clock.is_client_clock());
  EXPECT_FALSE(audio_clock.controls_tuneable_clock());
  EXPECT_TRUE(audio_clock.is_flexible());
  EXPECT_FALSE(audio_clock.is_tuneable());
  EXPECT_FALSE(audio_clock.controls_tuneable_clock());

  audio_clock = AudioClock::CreateAsCustom(clock::AdjustableCloneOfMonotonic());
  EXPECT_FALSE(audio_clock.is_device_clock());
  EXPECT_TRUE(audio_clock.is_client_clock());
  EXPECT_FALSE(audio_clock.is_flexible());
  EXPECT_FALSE(audio_clock.is_tuneable());
  EXPECT_FALSE(audio_clock.controls_tuneable_clock());

  constexpr uint32_t kCustomDomain = 42;
  audio_clock = AudioClock::CreateAsDeviceAdjustable(clock::CloneOfMonotonic(), kCustomDomain);
  EXPECT_TRUE(audio_clock.is_device_clock());
  EXPECT_FALSE(audio_clock.is_client_clock());
  EXPECT_FALSE(audio_clock.is_flexible());
  EXPECT_EQ(audio_clock.domain(), kCustomDomain);
  EXPECT_TRUE(audio_clock.is_tuneable());
  EXPECT_FALSE(audio_clock.controls_tuneable_clock());

  constexpr uint32_t kCustomDomain2 = 68;
  audio_clock = AudioClock::CreateAsDeviceStatic(clock::CloneOfMonotonic(), kCustomDomain2);
  EXPECT_TRUE(audio_clock.is_device_clock());
  EXPECT_FALSE(audio_clock.is_client_clock());
  EXPECT_FALSE(audio_clock.is_flexible());
  EXPECT_EQ(audio_clock.domain(), kCustomDomain2);
  EXPECT_FALSE(audio_clock.is_tuneable());
  EXPECT_FALSE(audio_clock.controls_tuneable_clock());
}

void VerifyCannotBeTuneableController(AudioClock& clock) {
  EXPECT_FALSE(clock.controls_tuneable_clock());

  EXPECT_FALSE(clock.set_controls_tuneable_clock(true));
  EXPECT_FALSE(clock.controls_tuneable_clock());

  EXPECT_FALSE(clock.set_controls_tuneable_clock(false));
  EXPECT_FALSE(clock.controls_tuneable_clock());
}

void VerifyCanBeTuneableController(AudioClock& clock) {
  EXPECT_FALSE(clock.controls_tuneable_clock());

  EXPECT_TRUE(clock.set_controls_tuneable_clock(true));
  EXPECT_TRUE(clock.controls_tuneable_clock());

  EXPECT_FALSE(clock.set_controls_tuneable_clock(false));
  EXPECT_FALSE(clock.controls_tuneable_clock());
}

TEST(AudioClockTest, set_controls_tuneable_clock) {
  constexpr uint32_t kCustomDomain = 42;
  auto flexible = AudioClock::CreateAsOptimal(clock::AdjustableCloneOfMonotonic());
  auto tuneable = AudioClock::CreateAsDeviceAdjustable(clock::CloneOfMonotonic(), kCustomDomain);
  auto device_static = AudioClock::CreateAsDeviceStatic(clock::CloneOfMonotonic(), kCustomDomain);
  VerifyCannotBeTuneableController(flexible);
  VerifyCannotBeTuneableController(tuneable);
  VerifyCannotBeTuneableController(device_static);

  auto custom = AudioClock::CreateAsCustom(clock::AdjustableCloneOfMonotonic());
  VerifyCanBeTuneableController(custom);
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

using ::media::audio::AudioClock;
TEST(AudioClockTest, SynchronizationMode) {
  auto custom = AudioClock::CreateAsCustom(clock::CloneOfMonotonic());
  auto custom2 = AudioClock::CreateAsCustom(clock::CloneOfMonotonic());

  auto hw_controlling = AudioClock::CreateAsCustom(clock::CloneOfMonotonic());
  auto hw_controlling2 = AudioClock::CreateAsCustom(clock::CloneOfMonotonic());
  hw_controlling.set_controls_tuneable_clock(true);
  hw_controlling2.set_controls_tuneable_clock(true);

  auto flex = AudioClock::CreateAsOptimal(clock::AdjustableCloneOfMonotonic());
  auto flex2 = AudioClock::CreateAsOptimal(clock::AdjustableCloneOfMonotonic());

  constexpr uint32_t kCustomDomain1 = 42;
  auto dev_static = AudioClock::CreateAsDeviceStatic(clock::CloneOfMonotonic(), kCustomDomain1);
  auto dev_static_same_domain =
      AudioClock::CreateAsDeviceStatic(clock::CloneOfMonotonic(), kCustomDomain1);

  auto dev_tuneable =
      AudioClock::CreateAsDeviceAdjustable(clock::CloneOfMonotonic(), kCustomDomain1);
  auto dev_tuneable_same_domain =
      AudioClock::CreateAsDeviceAdjustable(clock::CloneOfMonotonic(), kCustomDomain1);

  constexpr uint32_t kCustomDomain2 = 68;
  auto dev_static_diff_domain =
      AudioClock::CreateAsDeviceStatic(clock::CloneOfMonotonic(), kCustomDomain2);
  auto dev_tuneable_diff_domain =
      AudioClock::CreateAsDeviceAdjustable(clock::CloneOfMonotonic(), kCustomDomain2);

  auto none = AudioClock::SyncMode::None;
  // No synchronization is needed, when reconciling any clock with itself.
  EXPECT_EQ(none, AudioClock::SynchronizationMode(custom, custom));
  EXPECT_EQ(none, AudioClock::SynchronizationMode(hw_controlling, hw_controlling));
  EXPECT_EQ(none, AudioClock::SynchronizationMode(flex, flex));
  EXPECT_EQ(none, AudioClock::SynchronizationMode(dev_static, dev_static));
  EXPECT_EQ(none, AudioClock::SynchronizationMode(dev_tuneable, dev_tuneable));

  // No synchronization is needed, when reconciling two device clocks in the same domain.
  EXPECT_EQ(none, AudioClock::SynchronizationMode(dev_static, dev_static_same_domain));
  EXPECT_EQ(none, AudioClock::SynchronizationMode(dev_static, dev_tuneable_same_domain));
  EXPECT_EQ(none, AudioClock::SynchronizationMode(dev_tuneable, dev_static_same_domain));
  EXPECT_EQ(none, AudioClock::SynchronizationMode(dev_tuneable, dev_tuneable_same_domain));

  auto adjust_client = AudioClock::SyncMode::AdjustClientClock;
  // If a flexible clock is used, adjust it so that it matches the other clock.
  EXPECT_EQ(adjust_client, AudioClock::SynchronizationMode(flex, custom2));
  EXPECT_EQ(adjust_client, AudioClock::SynchronizationMode(flex, hw_controlling2));
  EXPECT_EQ(adjust_client, AudioClock::SynchronizationMode(flex, flex2));
  EXPECT_EQ(adjust_client, AudioClock::SynchronizationMode(flex, dev_static));
  EXPECT_EQ(adjust_client, AudioClock::SynchronizationMode(flex, dev_tuneable));

  EXPECT_EQ(adjust_client, AudioClock::SynchronizationMode(custom, flex));
  EXPECT_EQ(adjust_client, AudioClock::SynchronizationMode(hw_controlling, flex));
  EXPECT_EQ(adjust_client, AudioClock::SynchronizationMode(dev_static, flex));
  EXPECT_EQ(adjust_client, AudioClock::SynchronizationMode(dev_tuneable, flex));

  auto micro_src = AudioClock::SyncMode::MicroSrc;
  // If neither is Flexible, and if the clock pair does not include both a tuneable device clock and
  // the software clock designated to control it, then reconcile them using micro-SRC.
  EXPECT_EQ(micro_src, AudioClock::SynchronizationMode(custom, custom2));
  EXPECT_EQ(micro_src, AudioClock::SynchronizationMode(custom, hw_controlling));
  EXPECT_EQ(micro_src, AudioClock::SynchronizationMode(custom, dev_static));
  EXPECT_EQ(micro_src, AudioClock::SynchronizationMode(custom, dev_tuneable));

  EXPECT_EQ(micro_src, AudioClock::SynchronizationMode(hw_controlling, custom));
  EXPECT_EQ(micro_src, AudioClock::SynchronizationMode(hw_controlling, hw_controlling2));
  EXPECT_EQ(micro_src, AudioClock::SynchronizationMode(hw_controlling, dev_static));

  EXPECT_EQ(micro_src, AudioClock::SynchronizationMode(dev_static, custom));
  EXPECT_EQ(micro_src, AudioClock::SynchronizationMode(dev_static, hw_controlling));
  EXPECT_EQ(micro_src, AudioClock::SynchronizationMode(dev_static, dev_static_diff_domain));
  EXPECT_EQ(micro_src, AudioClock::SynchronizationMode(dev_static, dev_tuneable_diff_domain));

  EXPECT_EQ(micro_src, AudioClock::SynchronizationMode(dev_tuneable, custom2));
  EXPECT_EQ(micro_src, AudioClock::SynchronizationMode(dev_tuneable, dev_static_diff_domain));
  EXPECT_EQ(micro_src, AudioClock::SynchronizationMode(dev_tuneable, dev_tuneable_diff_domain));

  auto tune_hw = AudioClock::SyncMode::TuneHardware;
  // If one is a tuneable device clock and the other has been designated to control it, then tune
  // the hardware so that the device clock matches the other one.
  EXPECT_EQ(tune_hw, AudioClock::SynchronizationMode(dev_tuneable, hw_controlling2));
  EXPECT_EQ(tune_hw, AudioClock::SynchronizationMode(hw_controlling, dev_tuneable_diff_domain));
}

}  // namespace
}  // namespace media::audio
