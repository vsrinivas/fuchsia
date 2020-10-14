// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_clock.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>

#include <gtest/gtest.h>

#include "src/media/audio/lib/clock/clone_mono.h"

namespace media::audio {

class AudioClockTest : public testing::Test {
 protected:
  static constexpr uint32_t kCustomDomain = 42;
  static constexpr uint32_t kCustomDomain2 = 68;

  void ValidateSyncModeNone(AudioClock& source_clock, AudioClock& dest_clock) {
    ValidateSyncMode(AudioClock::SyncMode::None, source_clock, dest_clock);
  }
  void ValidateSyncModeSourceClock(AudioClock& source_clock, AudioClock& dest_clock) {
    ValidateSyncMode(AudioClock::SyncMode::AdjustSourceClock, source_clock, dest_clock);
  }
  void ValidateSyncModeDestClock(AudioClock& source_clock, AudioClock& dest_clock) {
    ValidateSyncMode(AudioClock::SyncMode::AdjustDestClock, source_clock, dest_clock);
  }
  void ValidateSyncModeSourceHardware(AudioClock& source_clock, AudioClock& dest_clock) {
    ValidateSyncMode(AudioClock::SyncMode::AdjustSourceHardware, source_clock, dest_clock);
  }
  void ValidateSyncModeDestHardware(AudioClock& source_clock, AudioClock& dest_clock) {
    ValidateSyncMode(AudioClock::SyncMode::AdjustDestHardware, source_clock, dest_clock);
  }
  void ValidateSyncModeMicroSrc(AudioClock& source_clock, AudioClock& dest_clock) {
    ValidateSyncMode(AudioClock::SyncMode::MicroSrc, source_clock, dest_clock);
  }

 private:
  void ValidateSyncMode(AudioClock::SyncMode mode, AudioClock& source_clock,
                        AudioClock& dest_clock) {
    EXPECT_EQ(mode, AudioClock::SynchronizationMode(source_clock, dest_clock));
  }
};

TEST_F(AudioClockTest, EqualsOperator) {
  auto clock1 = AudioClock::CreateAsClientAdjustable(clock::AdjustableCloneOfMonotonic());
  auto clock2 = AudioClock::CreateAsClientAdjustable(clock::AdjustableCloneOfMonotonic());
  EXPECT_FALSE(clock1 == clock2);

  auto clock3 = AudioClock::CreateAsClientNonadjustable(clock1.DuplicateClock());
  EXPECT_TRUE(clock1 == clock3);
}

// Clock type (and domain, for Device clocks) can be set after construction.
TEST_F(AudioClockTest, CreateAs) {
  auto audio_clock = AudioClock::CreateAsClientAdjustable(clock::AdjustableCloneOfMonotonic());
  EXPECT_FALSE(audio_clock.is_device_clock());
  EXPECT_TRUE(audio_clock.is_client_clock());
  EXPECT_FALSE(audio_clock.controls_device_clock());
  EXPECT_TRUE(audio_clock.is_adjustable());
  EXPECT_FALSE(audio_clock.controls_device_clock());

  audio_clock = AudioClock::CreateAsClientNonadjustable(clock::CloneOfMonotonic());
  EXPECT_FALSE(audio_clock.is_device_clock());
  EXPECT_TRUE(audio_clock.is_client_clock());
  EXPECT_FALSE(audio_clock.is_adjustable());
  EXPECT_FALSE(audio_clock.controls_device_clock());

  audio_clock =
      AudioClock::CreateAsDeviceAdjustable(clock::AdjustableCloneOfMonotonic(), kCustomDomain);
  EXPECT_TRUE(audio_clock.is_device_clock());
  EXPECT_FALSE(audio_clock.is_client_clock());
  EXPECT_TRUE(audio_clock.is_adjustable());
  EXPECT_FALSE(audio_clock.controls_device_clock());

  audio_clock = AudioClock::CreateAsDeviceNonadjustable(clock::CloneOfMonotonic(), kCustomDomain2);
  EXPECT_TRUE(audio_clock.is_device_clock());
  EXPECT_FALSE(audio_clock.is_client_clock());
  EXPECT_FALSE(audio_clock.is_adjustable());
  EXPECT_FALSE(audio_clock.controls_device_clock());
}

void VerifyCannotControlDeviceClock(AudioClock& clock) {
  EXPECT_FALSE(clock.controls_device_clock());

  clock.set_controls_device_clock(true);
  EXPECT_FALSE(clock.controls_device_clock());
}

void VerifyCanControlDeviceClock(AudioClock& clock) {
  EXPECT_FALSE(clock.controls_device_clock());

  clock.set_controls_device_clock(true);
  EXPECT_TRUE(clock.controls_device_clock());

  clock.set_controls_device_clock(false);
  EXPECT_FALSE(clock.controls_device_clock());
}

TEST_F(AudioClockTest, set_controls_device_clock) {
  auto client_adjustable =
      AudioClock::CreateAsClientAdjustable(clock::AdjustableCloneOfMonotonic());
  auto dev_adjust =
      AudioClock::CreateAsDeviceAdjustable(clock::AdjustableCloneOfMonotonic(), kCustomDomain);
  auto dev_nonadjust =
      AudioClock::CreateAsDeviceNonadjustable(clock::CloneOfMonotonic(), kCustomDomain);
  VerifyCannotControlDeviceClock(client_adjustable);
  VerifyCannotControlDeviceClock(dev_adjust);
  VerifyCannotControlDeviceClock(dev_nonadjust);

  auto client_nonadjust = AudioClock::CreateAsClientNonadjustable(clock::CloneOfMonotonic());
  VerifyCanControlDeviceClock(client_nonadjust);
}

TEST_F(AudioClockTest, ClockMonoToRefClock) {
  auto clock = clock::AdjustableCloneOfMonotonic();

  zx::clock::update_args args;
  args.reset().set_rate_adjust(-1000);
  EXPECT_EQ(clock.update(args), ZX_OK) << "clock.update with rate_adjust failed";

  auto audio_clock = AudioClock::CreateAsClientNonadjustable(std::move(clock));

  auto post_update_tl_func = audio_clock.ref_clock_to_clock_mono();
  EXPECT_LT(post_update_tl_func.reference_delta(), post_update_tl_func.subject_delta())
      << "rate should be less than 1:1";
}

TEST_F(AudioClockTest, DuplicateClock) {
  auto audio_clock = AudioClock::CreateAsClientNonadjustable(clock::CloneOfMonotonic());

  auto dupe_raw_clock = audio_clock.DuplicateClock();
  EXPECT_TRUE(dupe_raw_clock.is_valid());

  auto dupe_audio_clock = AudioClock::CreateAsClientNonadjustable(std::move(dupe_raw_clock));

  auto time1 = dupe_audio_clock.Read().get();
  auto time2 = dupe_audio_clock.Read().get();

  EXPECT_LT(time1, time2);
}

TEST_F(AudioClockTest, InvalidZxClockHaltsCreate) {
  // Uninitialized clock cannot be passed to CreateAs...
  ASSERT_DEATH(AudioClock::CreateAsClientNonadjustable(zx::clock()), "");
  ASSERT_DEATH(AudioClock::CreateAsClientAdjustable(zx::clock()), "");
  ASSERT_DEATH(AudioClock::CreateAsDeviceNonadjustable(zx::clock(), kCustomDomain), "");
  ASSERT_DEATH(AudioClock::CreateAsDeviceAdjustable(zx::clock(), kCustomDomain), "");

  // Clock without WRITE rights cannot be passed to CreateAs...Adjustable
  ASSERT_DEATH(AudioClock::CreateAsClientAdjustable(clock::CloneOfMonotonic()), "");
  ASSERT_DEATH(AudioClock::CreateAsDeviceAdjustable(clock::CloneOfMonotonic(), kCustomDomain), "");
}

// Validate AudioClock::SynchronizationMode() combinations leading to SyncMode::None
TEST_F(AudioClockTest, SynchronizationMode_None) {
  auto cli_nonadjust = AudioClock::CreateAsClientNonadjustable(clock::CloneOfMonotonic());

  auto hw_controlling = AudioClock::CreateAsClientNonadjustable(clock::CloneOfMonotonic());
  hw_controlling.set_controls_device_clock(true);

  auto cli_adjust = AudioClock::CreateAsClientAdjustable(clock::AdjustableCloneOfMonotonic());

  auto dev_nonadjust =
      AudioClock::CreateAsDeviceNonadjustable(clock::CloneOfMonotonic(), kCustomDomain);
  auto dev_nonadjust_same_domain =
      AudioClock::CreateAsDeviceNonadjustable(clock::CloneOfMonotonic(), kCustomDomain);

  auto dev_adjust =
      AudioClock::CreateAsDeviceAdjustable(clock::AdjustableCloneOfMonotonic(), kCustomDomain);
  auto dev_adjust_same_domain =
      AudioClock::CreateAsDeviceAdjustable(clock::AdjustableCloneOfMonotonic(), kCustomDomain);

  // No synchronization is needed, when reconciling any clock with itself.
  ValidateSyncModeNone(cli_nonadjust, cli_nonadjust);
  ValidateSyncModeNone(hw_controlling, hw_controlling);
  ValidateSyncModeNone(cli_adjust, cli_adjust);
  ValidateSyncModeNone(dev_nonadjust, dev_nonadjust);
  ValidateSyncModeNone(dev_adjust, dev_adjust);

  // No synchronization is needed, when reconciling two device clocks in the same domain.
  ValidateSyncModeNone(dev_nonadjust, dev_nonadjust_same_domain);
  ValidateSyncModeNone(dev_nonadjust, dev_adjust_same_domain);
  ValidateSyncModeNone(dev_adjust, dev_nonadjust_same_domain);
  ValidateSyncModeNone(dev_adjust, dev_adjust_same_domain);
}

// Validate AudioClock::SynchronizationMode() combinations leading to SyncMode::AdjustClientClock
TEST_F(AudioClockTest, SynchronizationMode_AdjustClientClock) {
  auto cli_nonadjust = AudioClock::CreateAsClientNonadjustable(clock::CloneOfMonotonic());
  auto cli_nonadjust2 = AudioClock::CreateAsClientNonadjustable(clock::CloneOfMonotonic());

  auto hw_controlling = AudioClock::CreateAsClientNonadjustable(clock::CloneOfMonotonic());
  auto hw_controlling2 = AudioClock::CreateAsClientNonadjustable(clock::CloneOfMonotonic());
  hw_controlling.set_controls_device_clock(true);
  hw_controlling2.set_controls_device_clock(true);

  auto cli_adjust = AudioClock::CreateAsClientAdjustable(clock::AdjustableCloneOfMonotonic());
  auto cli_adjust2 = AudioClock::CreateAsClientAdjustable(clock::AdjustableCloneOfMonotonic());

  auto dev_nonadjust =
      AudioClock::CreateAsDeviceNonadjustable(clock::CloneOfMonotonic(), kCustomDomain);

  auto dev_adjust =
      AudioClock::CreateAsDeviceAdjustable(clock::AdjustableCloneOfMonotonic(), kCustomDomain);

  // If a client adjustable clock is used, adjust it so that it matches the other clock.
  ValidateSyncModeSourceClock(cli_adjust, cli_nonadjust2);
  ValidateSyncModeSourceClock(cli_adjust, hw_controlling2);
  ValidateSyncModeSourceClock(cli_adjust, cli_adjust2);
  ValidateSyncModeSourceClock(cli_adjust, dev_nonadjust);
  ValidateSyncModeSourceClock(cli_adjust, dev_adjust);

  ValidateSyncModeDestClock(cli_nonadjust, cli_adjust);
  ValidateSyncModeDestClock(hw_controlling, cli_adjust);
  ValidateSyncModeDestClock(dev_nonadjust, cli_adjust);
  ValidateSyncModeDestClock(dev_adjust, cli_adjust);
}

// Validate AudioClock::SynchronizationMode() combinations leading to SyncMode::MicroSrc
TEST_F(AudioClockTest, SynchronizationMode_MicroSrc) {
  auto cli_nonadjust = AudioClock::CreateAsClientNonadjustable(clock::CloneOfMonotonic());
  auto cli_nonadjust2 = AudioClock::CreateAsClientNonadjustable(clock::CloneOfMonotonic());

  auto hw_controlling = AudioClock::CreateAsClientNonadjustable(clock::CloneOfMonotonic());
  auto hw_controlling2 = AudioClock::CreateAsClientNonadjustable(clock::CloneOfMonotonic());
  hw_controlling.set_controls_device_clock(true);
  hw_controlling2.set_controls_device_clock(true);

  auto dev_nonadjust =
      AudioClock::CreateAsDeviceNonadjustable(clock::CloneOfMonotonic(), kCustomDomain);
  auto dev_nonadjust_diff_domain =
      AudioClock::CreateAsDeviceNonadjustable(clock::CloneOfMonotonic(), kCustomDomain2);

  auto dev_adjust =
      AudioClock::CreateAsDeviceAdjustable(clock::AdjustableCloneOfMonotonic(), kCustomDomain);
  auto dev_adjust_diff_domain =
      AudioClock::CreateAsDeviceAdjustable(clock::AdjustableCloneOfMonotonic(), kCustomDomain2);

  // If neither is Flexible, and if the clock pair does not include both a adjustable device clock
  // and the software clock designated to control it, then reconcile them using micro-SRC.
  ValidateSyncModeMicroSrc(cli_nonadjust, cli_nonadjust2);
  ValidateSyncModeMicroSrc(cli_nonadjust, hw_controlling);
  ValidateSyncModeMicroSrc(cli_nonadjust, dev_nonadjust);
  ValidateSyncModeMicroSrc(cli_nonadjust, dev_adjust);

  ValidateSyncModeMicroSrc(hw_controlling, cli_nonadjust);
  ValidateSyncModeMicroSrc(hw_controlling, hw_controlling2);
  ValidateSyncModeMicroSrc(hw_controlling, dev_nonadjust);

  ValidateSyncModeMicroSrc(dev_nonadjust, cli_nonadjust);
  ValidateSyncModeMicroSrc(dev_nonadjust, hw_controlling);
  ValidateSyncModeMicroSrc(dev_nonadjust, dev_nonadjust_diff_domain);
  ValidateSyncModeMicroSrc(dev_nonadjust, dev_adjust_diff_domain);

  ValidateSyncModeMicroSrc(dev_adjust, cli_nonadjust);
  ValidateSyncModeMicroSrc(dev_adjust, dev_nonadjust_diff_domain);
  ValidateSyncModeMicroSrc(dev_adjust, dev_adjust_diff_domain);
}

// Validate AudioClock::SynchronizationMode() combinations leading to SyncMode::AdjustHardwareClock
TEST_F(AudioClockTest, SynchronizationMode_AdjustHardwareClock) {
  auto hw_controlling = AudioClock::CreateAsClientNonadjustable(clock::CloneOfMonotonic());
  auto hw_controlling2 = AudioClock::CreateAsClientNonadjustable(clock::CloneOfMonotonic());
  hw_controlling.set_controls_device_clock(true);
  hw_controlling2.set_controls_device_clock(true);

  auto dev_adjust =
      AudioClock::CreateAsDeviceAdjustable(clock::AdjustableCloneOfMonotonic(), kCustomDomain);
  auto dev_adjust_diff_domain =
      AudioClock::CreateAsDeviceAdjustable(clock::AdjustableCloneOfMonotonic(), kCustomDomain2);

  // If one is a adjustable device clock and the other has been designated to control it, then
  // adjust the hardware so that the device clock matches the other one.
  ValidateSyncModeSourceHardware(dev_adjust, hw_controlling2);
  ValidateSyncModeDestHardware(hw_controlling, dev_adjust_diff_domain);
}

}  // namespace media::audio
