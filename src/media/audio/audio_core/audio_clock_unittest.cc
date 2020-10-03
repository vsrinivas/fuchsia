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

constexpr uint32_t kCustomDomain = 42;
constexpr uint32_t kCustomDomain2 = 68;

// Verify default values
TEST(AudioClockTest, Defaults) {
  AudioClock audio_clock;
  EXPECT_FALSE(audio_clock);
  EXPECT_FALSE(audio_clock.is_valid());
  EXPECT_FALSE(audio_clock.is_device_clock());
  EXPECT_FALSE(audio_clock.is_client_clock());
  EXPECT_FALSE(audio_clock.is_adjustable());
  EXPECT_FALSE(audio_clock.controls_device_clock());
}

TEST(AudioClockTest, ComparisonOperator) {
  auto clock1 = AudioClock::CreateAsClientAdjustable(clock::AdjustableCloneOfMonotonic());
  auto clock2 = AudioClock::CreateAsClientAdjustable(clock::AdjustableCloneOfMonotonic());
  EXPECT_FALSE(clock1 == clock2);

  auto clock3 = AudioClock::CreateAsClientNonadjustable(clock1.DuplicateClock());
  EXPECT_TRUE(clock1 == clock3);
}

// Verify operator bool and is_valid(), and being subsequently set
TEST(AudioClockTest, Basic) {
  AudioClock default_audio_clock;
  EXPECT_FALSE(default_audio_clock);
  EXPECT_FALSE(default_audio_clock.is_valid());

  auto audio_clock = AudioClock::CreateAsClientNonadjustable(clock::CloneOfMonotonic());
  EXPECT_TRUE(audio_clock);
  EXPECT_TRUE(audio_clock.is_valid());

  auto time1 = audio_clock.Read().get();
  auto time2 = audio_clock.Read().get();

  EXPECT_LT(time1, time2);
}

// Clock type (and domain, for Device clocks) can be set after construction.
TEST(AudioClockTest, CreateAs) {
  AudioClock audio_clock;
  audio_clock = AudioClock::CreateAsClientAdjustable(clock::AdjustableCloneOfMonotonic());
  EXPECT_FALSE(audio_clock.is_device_clock());
  EXPECT_TRUE(audio_clock.is_client_clock());
  EXPECT_FALSE(audio_clock.controls_device_clock());
  EXPECT_TRUE(audio_clock.is_adjustable());
  EXPECT_FALSE(audio_clock.controls_device_clock());

  audio_clock = AudioClock::CreateAsClientNonadjustable(clock::AdjustableCloneOfMonotonic());
  EXPECT_FALSE(audio_clock.is_device_clock());
  EXPECT_TRUE(audio_clock.is_client_clock());
  EXPECT_FALSE(audio_clock.is_adjustable());
  EXPECT_FALSE(audio_clock.controls_device_clock());

  audio_clock = AudioClock::CreateAsDeviceAdjustable(clock::CloneOfMonotonic(), kCustomDomain);
  EXPECT_TRUE(audio_clock.is_device_clock());
  EXPECT_FALSE(audio_clock.is_client_clock());
  EXPECT_EQ(audio_clock.domain(), kCustomDomain);
  EXPECT_TRUE(audio_clock.is_adjustable());
  EXPECT_FALSE(audio_clock.controls_device_clock());

  audio_clock = AudioClock::CreateAsDeviceNonadjustable(clock::CloneOfMonotonic(), kCustomDomain2);
  EXPECT_TRUE(audio_clock.is_device_clock());
  EXPECT_FALSE(audio_clock.is_client_clock());
  EXPECT_EQ(audio_clock.domain(), kCustomDomain2);
  EXPECT_FALSE(audio_clock.is_adjustable());
  EXPECT_FALSE(audio_clock.controls_device_clock());
}

void VerifyCannotControlDeviceClock(AudioClock& clock) {
  EXPECT_FALSE(clock.controls_device_clock());

  EXPECT_FALSE(clock.set_controls_device_clock(true));
  EXPECT_FALSE(clock.controls_device_clock());

  EXPECT_FALSE(clock.set_controls_device_clock(false));
  EXPECT_FALSE(clock.controls_device_clock());
}

void VerifyCanControlDeviceClock(AudioClock& clock) {
  EXPECT_FALSE(clock.controls_device_clock());

  EXPECT_TRUE(clock.set_controls_device_clock(true));
  EXPECT_TRUE(clock.controls_device_clock());

  EXPECT_FALSE(clock.set_controls_device_clock(false));
  EXPECT_FALSE(clock.controls_device_clock());
}

TEST(AudioClockTest, set_controls_device_clock) {
  auto client_adjustable =
      AudioClock::CreateAsClientAdjustable(clock::AdjustableCloneOfMonotonic());
  auto dev_adjustable =
      AudioClock::CreateAsDeviceAdjustable(clock::CloneOfMonotonic(), kCustomDomain);
  auto dev_nonadjustable =
      AudioClock::CreateAsDeviceNonadjustable(clock::CloneOfMonotonic(), kCustomDomain);
  VerifyCannotControlDeviceClock(client_adjustable);
  VerifyCannotControlDeviceClock(dev_adjustable);
  VerifyCannotControlDeviceClock(dev_nonadjustable);

  auto client_nonadjustable =
      AudioClock::CreateAsClientNonadjustable(clock::AdjustableCloneOfMonotonic());
  VerifyCanControlDeviceClock(client_nonadjustable);
}

TEST(AudioClockTest, ClockMonoToRefClock) {
  auto clock = clock::AdjustableCloneOfMonotonic();

  zx::clock::update_args args;
  args.reset().set_rate_adjust(-1000);
  EXPECT_EQ(clock.update(args), ZX_OK) << "clock.update with rate_adjust failed";

  auto audio_clock = AudioClock::CreateAsClientNonadjustable(std::move(clock));

  auto post_update_tl_func = audio_clock.ref_clock_to_clock_mono();
  EXPECT_LT(post_update_tl_func.reference_delta(), post_update_tl_func.subject_delta())
      << "rate should be less than 1:1";
}

TEST(AudioClockTest, DuplicateClock) {
  auto audio_clock = AudioClock::CreateAsClientNonadjustable(clock::CloneOfMonotonic());

  auto dupe_clock = audio_clock.DuplicateClock();
  EXPECT_TRUE(dupe_clock.is_valid());

  auto dupe_audio_clock = AudioClock::CreateAsClientNonadjustable(std::move(dupe_clock));

  auto time1 = dupe_audio_clock.Read().get();
  auto time2 = dupe_audio_clock.Read().get();

  EXPECT_LT(time1, time2);
}

TEST(AudioClockTest, UninitializedAudioClockCausesHalt) {
  AudioClock uninitialized_audio_clock;

  ASSERT_DEATH(uninitialized_audio_clock.DuplicateClock(), "");
  ASSERT_DEATH(uninitialized_audio_clock.Read(), "");

  ASSERT_DEATH(uninitialized_audio_clock.ref_clock_to_clock_mono(), "");
  ASSERT_DEATH(uninitialized_audio_clock.ReferenceTimeFromMonotonicTime(zx::time{0}), "");
  ASSERT_DEATH(uninitialized_audio_clock.MonotonicTimeFromReferenceTime(zx::time{0}), "");

  ASSERT_DEATH(uninitialized_audio_clock.TuneRateForError(0, Fixed{0}), "");
  ASSERT_DEATH(uninitialized_audio_clock.ResetRateAdjustment(0), "");

  // set_controls_device_clock
  // ConfigureAdjustment
}

TEST(AudioClockTest, InvalidZxClockHaltsCreate) {
  ASSERT_DEATH(AudioClock::CreateAsClientNonadjustable(zx::clock()), "");
  ASSERT_DEATH(AudioClock::CreateAsClientAdjustable(zx::clock()), "");
  ASSERT_DEATH(AudioClock::CreateAsDeviceNonadjustable(zx::clock(), kCustomDomain), "");
  ASSERT_DEATH(AudioClock::CreateAsDeviceAdjustable(zx::clock(), kCustomDomain), "");
}

using ::media::audio::AudioClock;
TEST(AudioClockTest, SynchronizationMode) {
  auto client_nonadjustable = AudioClock::CreateAsClientNonadjustable(clock::CloneOfMonotonic());
  auto client_nonadjustable2 = AudioClock::CreateAsClientNonadjustable(clock::CloneOfMonotonic());

  auto hw_controlling = AudioClock::CreateAsClientNonadjustable(clock::CloneOfMonotonic());
  auto hw_controlling2 = AudioClock::CreateAsClientNonadjustable(clock::CloneOfMonotonic());
  hw_controlling.set_controls_device_clock(true);
  hw_controlling2.set_controls_device_clock(true);

  auto flex = AudioClock::CreateAsClientAdjustable(clock::AdjustableCloneOfMonotonic());
  auto flex2 = AudioClock::CreateAsClientAdjustable(clock::AdjustableCloneOfMonotonic());

  auto dev_nonadjustable =
      AudioClock::CreateAsDeviceNonadjustable(clock::CloneOfMonotonic(), kCustomDomain);
  auto dev_nonadjustable_same_domain =
      AudioClock::CreateAsDeviceNonadjustable(clock::CloneOfMonotonic(), kCustomDomain);

  auto dev_adjustable =
      AudioClock::CreateAsDeviceAdjustable(clock::CloneOfMonotonic(), kCustomDomain);
  auto dev_adjustable_same_domain =
      AudioClock::CreateAsDeviceAdjustable(clock::CloneOfMonotonic(), kCustomDomain);

  auto dev_nonadjustable_diff_domain =
      AudioClock::CreateAsDeviceNonadjustable(clock::CloneOfMonotonic(), kCustomDomain2);
  auto dev_adjustable_diff_domain =
      AudioClock::CreateAsDeviceAdjustable(clock::CloneOfMonotonic(), kCustomDomain2);

  auto none = AudioClock::SyncMode::None;
  // No synchronization is needed, when reconciling any clock with itself.
  EXPECT_EQ(none, AudioClock::SynchronizationMode(client_nonadjustable, client_nonadjustable));
  EXPECT_EQ(none, AudioClock::SynchronizationMode(hw_controlling, hw_controlling));
  EXPECT_EQ(none, AudioClock::SynchronizationMode(flex, flex));
  EXPECT_EQ(none, AudioClock::SynchronizationMode(dev_nonadjustable, dev_nonadjustable));
  EXPECT_EQ(none, AudioClock::SynchronizationMode(dev_adjustable, dev_adjustable));

  // No synchronization is needed, when reconciling two device clocks in the same domain.
  EXPECT_EQ(none,
            AudioClock::SynchronizationMode(dev_nonadjustable, dev_nonadjustable_same_domain));
  EXPECT_EQ(none, AudioClock::SynchronizationMode(dev_nonadjustable, dev_adjustable_same_domain));
  EXPECT_EQ(none, AudioClock::SynchronizationMode(dev_adjustable, dev_nonadjustable_same_domain));
  EXPECT_EQ(none, AudioClock::SynchronizationMode(dev_adjustable, dev_adjustable_same_domain));

  auto adjust_client = AudioClock::SyncMode::AdjustClientClock;
  // If a client adjustable clock is used, adjust it so that it matches the other clock.
  EXPECT_EQ(adjust_client, AudioClock::SynchronizationMode(flex, client_nonadjustable2));
  EXPECT_EQ(adjust_client, AudioClock::SynchronizationMode(flex, hw_controlling2));
  EXPECT_EQ(adjust_client, AudioClock::SynchronizationMode(flex, flex2));
  EXPECT_EQ(adjust_client, AudioClock::SynchronizationMode(flex, dev_nonadjustable));
  EXPECT_EQ(adjust_client, AudioClock::SynchronizationMode(flex, dev_adjustable));

  EXPECT_EQ(adjust_client, AudioClock::SynchronizationMode(client_nonadjustable, flex));
  EXPECT_EQ(adjust_client, AudioClock::SynchronizationMode(hw_controlling, flex));
  EXPECT_EQ(adjust_client, AudioClock::SynchronizationMode(dev_nonadjustable, flex));
  EXPECT_EQ(adjust_client, AudioClock::SynchronizationMode(dev_adjustable, flex));

  auto micro_src = AudioClock::SyncMode::MicroSrc;
  // If neither is Flexible, and if the clock pair does not include both a adjustable device clock
  // and the software clock designated to control it, then reconcile them using micro-SRC.
  EXPECT_EQ(micro_src,
            AudioClock::SynchronizationMode(client_nonadjustable, client_nonadjustable2));
  EXPECT_EQ(micro_src, AudioClock::SynchronizationMode(client_nonadjustable, hw_controlling));
  EXPECT_EQ(micro_src, AudioClock::SynchronizationMode(client_nonadjustable, dev_nonadjustable));
  EXPECT_EQ(micro_src, AudioClock::SynchronizationMode(client_nonadjustable, dev_adjustable));

  EXPECT_EQ(micro_src, AudioClock::SynchronizationMode(hw_controlling, client_nonadjustable));
  EXPECT_EQ(micro_src, AudioClock::SynchronizationMode(hw_controlling, hw_controlling2));
  EXPECT_EQ(micro_src, AudioClock::SynchronizationMode(hw_controlling, dev_nonadjustable));

  EXPECT_EQ(micro_src, AudioClock::SynchronizationMode(dev_nonadjustable, client_nonadjustable));
  EXPECT_EQ(micro_src, AudioClock::SynchronizationMode(dev_nonadjustable, hw_controlling));
  EXPECT_EQ(micro_src,
            AudioClock::SynchronizationMode(dev_nonadjustable, dev_nonadjustable_diff_domain));
  EXPECT_EQ(micro_src,
            AudioClock::SynchronizationMode(dev_nonadjustable, dev_adjustable_diff_domain));

  EXPECT_EQ(micro_src, AudioClock::SynchronizationMode(dev_adjustable, client_nonadjustable2));
  EXPECT_EQ(micro_src,
            AudioClock::SynchronizationMode(dev_adjustable, dev_nonadjustable_diff_domain));
  EXPECT_EQ(micro_src, AudioClock::SynchronizationMode(dev_adjustable, dev_adjustable_diff_domain));

  auto adjust_hw = AudioClock::SyncMode::AdjustHardwareClock;
  // If one is a adjustable device clock and the other has been designated to control it, then
  // adjust the hardware so that the device clock matches the other one.
  EXPECT_EQ(adjust_hw, AudioClock::SynchronizationMode(dev_adjustable, hw_controlling2));
  EXPECT_EQ(adjust_hw, AudioClock::SynchronizationMode(hw_controlling, dev_adjustable_diff_domain));
}

}  // namespace
}  // namespace media::audio
