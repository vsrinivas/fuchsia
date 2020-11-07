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

  // Convenience methods so AudioClock only has to declare one class (AudioClockTest) as friend.
  void ValidateSyncNone(AudioClock& source_clock, AudioClock& dest_clock) {
    EXPECT_EQ(AudioClock::SyncMode::None, AudioClock::SyncModeForClocks(source_clock, dest_clock));
  }
  void ValidateSyncResetSource(AudioClock& source_clock, AudioClock& dest_clock) {
    EXPECT_EQ(AudioClock::SyncMode::ResetSourceClock,
              AudioClock::SyncModeForClocks(source_clock, dest_clock));
  }
  void ValidateSyncResetDest(AudioClock& source_clock, AudioClock& dest_clock) {
    EXPECT_EQ(AudioClock::SyncMode::ResetDestClock,
              AudioClock::SyncModeForClocks(source_clock, dest_clock));
  }
  void ValidateSyncAdjustSource(AudioClock& source_clock, AudioClock& dest_clock) {
    EXPECT_EQ(AudioClock::SyncMode::AdjustSourceClock,
              AudioClock::SyncModeForClocks(source_clock, dest_clock));
  }
  void ValidateSyncAdjustDest(AudioClock& source_clock, AudioClock& dest_clock) {
    EXPECT_EQ(AudioClock::SyncMode::AdjustDestClock,
              AudioClock::SyncModeForClocks(source_clock, dest_clock));
  }
  void ValidateSyncMicroSrc(AudioClock& source_clock, AudioClock& dest_clock) {
    EXPECT_EQ(AudioClock::SyncMode::MicroSrc,
              AudioClock::SyncModeForClocks(source_clock, dest_clock));
  }
};

TEST_F(AudioClockTest, EqualsOperator) {
  auto clock1 = AudioClock::ClientAdjustable(clock::AdjustableCloneOfMonotonic());
  auto clock2 = AudioClock::ClientAdjustable(clock::AdjustableCloneOfMonotonic());
  EXPECT_FALSE(clock1 == clock2);

  auto clock3 = AudioClock::ClientFixed(clock1.DuplicateClock());
  EXPECT_TRUE(clock1 == clock3);
}

TEST_F(AudioClockTest, CreateClientAdjustable) {
  auto audio_clock = AudioClock::ClientAdjustable(clock::AdjustableCloneOfMonotonic());
  EXPECT_FALSE(audio_clock.is_device_clock());
  EXPECT_TRUE(audio_clock.is_client_clock());
  EXPECT_TRUE(audio_clock.is_adjustable());
}

TEST_F(AudioClockTest, CreateClientFixed) {
  auto audio_clock = AudioClock::ClientFixed(clock::CloneOfMonotonic());
  EXPECT_FALSE(audio_clock.is_device_clock());
  EXPECT_TRUE(audio_clock.is_client_clock());
  EXPECT_FALSE(audio_clock.is_adjustable());
}

TEST_F(AudioClockTest, CreateDeviceAdjustable) {
  auto audio_clock =
      AudioClock::DeviceAdjustable(clock::AdjustableCloneOfMonotonic(), kCustomDomain);
  EXPECT_TRUE(audio_clock.is_device_clock());
  EXPECT_FALSE(audio_clock.is_client_clock());
  EXPECT_TRUE(audio_clock.is_adjustable());
}

TEST_F(AudioClockTest, CreateDeviceFixed) {
  auto audio_clock = AudioClock::DeviceFixed(clock::CloneOfMonotonic(), kCustomDomain2);
  EXPECT_TRUE(audio_clock.is_device_clock());
  EXPECT_FALSE(audio_clock.is_client_clock());
  EXPECT_FALSE(audio_clock.is_adjustable());
}

TEST_F(AudioClockTest, ClockMonoToRefClock) {
  auto clock = clock::AdjustableCloneOfMonotonic();

  zx::clock::update_args args;
  args.reset().set_rate_adjust(-1000);
  EXPECT_EQ(clock.update(args), ZX_OK) << "clock.update with rate_adjust failed";

  auto audio_clock = AudioClock::ClientFixed(std::move(clock));

  auto post_update_tl_func = audio_clock.ref_clock_to_clock_mono();
  EXPECT_LT(post_update_tl_func.reference_delta(), post_update_tl_func.subject_delta())
      << "rate should be less than 1:1";
}

TEST_F(AudioClockTest, DuplicateClock) {
  auto audio_clock = AudioClock::ClientFixed(clock::CloneOfMonotonic());

  auto dupe_raw_clock = audio_clock.DuplicateClock();
  EXPECT_TRUE(dupe_raw_clock.is_valid());

  auto dupe_audio_clock = AudioClock::ClientFixed(std::move(dupe_raw_clock));

  auto time1 = dupe_audio_clock.Read().get();
  auto time2 = dupe_audio_clock.Read().get();

  EXPECT_LT(time1, time2);
}

TEST_F(AudioClockTest, InvalidZxClockHaltsCreate) {
  // Uninitialized clock cannot be passed to CreateAs...
  ASSERT_DEATH(AudioClock::ClientFixed(zx::clock()), "");
  ASSERT_DEATH(AudioClock::ClientAdjustable(zx::clock()), "");
  ASSERT_DEATH(AudioClock::DeviceFixed(zx::clock(), kCustomDomain), "");
  ASSERT_DEATH(AudioClock::DeviceAdjustable(zx::clock(), kCustomDomain), "");

  // Clock without WRITE rights cannot be passed to CreateAs...Adjustable
  ASSERT_DEATH(AudioClock::ClientAdjustable(clock::CloneOfMonotonic()), "");
  ASSERT_DEATH(AudioClock::DeviceAdjustable(clock::CloneOfMonotonic(), kCustomDomain), "");
}

// Validate AudioClock::SyncModeForClocks() combinations leading to SyncMode::None
TEST_F(AudioClockTest, SyncModeNone) {
  auto client_fixed = AudioClock::ClientFixed(clock::CloneOfMonotonic());

  auto client_adjustable = AudioClock::ClientAdjustable(clock::AdjustableCloneOfMonotonic());

  auto device_monotonic =
      AudioClock::DeviceFixed(clock::CloneOfMonotonic(), AudioClock::kMonotonicDomain);
  auto device_monotonic2 =
      AudioClock::DeviceFixed(clock::CloneOfMonotonic(), AudioClock::kMonotonicDomain);

  auto device_non_monotonic = AudioClock::DeviceFixed(clock::CloneOfMonotonic(), kCustomDomain);
  auto device_fixed_same_domain = AudioClock::DeviceFixed(clock::CloneOfMonotonic(), kCustomDomain);

  auto device_adjustable =
      AudioClock::DeviceAdjustable(clock::AdjustableCloneOfMonotonic(), kCustomDomain);
  auto device_adjustable_same_domain =
      AudioClock::DeviceAdjustable(clock::AdjustableCloneOfMonotonic(), kCustomDomain);

  // No synchronization is needed, when reconciling any clock with itself.
  ValidateSyncNone(client_fixed, client_fixed);
  ValidateSyncNone(client_adjustable, client_adjustable);
  ValidateSyncNone(device_monotonic, device_monotonic);
  ValidateSyncNone(device_monotonic, device_monotonic2);
  ValidateSyncNone(device_non_monotonic, device_non_monotonic);
  ValidateSyncNone(device_adjustable, device_adjustable);

  // No synchronization is needed, when reconciling two device clocks in the same domain.
  ValidateSyncNone(device_non_monotonic, device_fixed_same_domain);
  ValidateSyncNone(device_non_monotonic, device_adjustable_same_domain);
  ValidateSyncNone(device_adjustable, device_fixed_same_domain);
  ValidateSyncNone(device_adjustable, device_adjustable_same_domain);
}

// Validate AudioClock::SyncModeForClocks() combinations leading to SyncMode::ResetSourceClock
TEST_F(AudioClockTest, SyncModeResetSourceClock) {
  auto client_adjustable = AudioClock::ClientAdjustable(clock::AdjustableCloneOfMonotonic());

  auto device_monotonic =
      AudioClock::DeviceFixed(clock::CloneOfMonotonic(), AudioClock::kMonotonicDomain);

  // If device is in MONOTONIC domain, adjustable client clock can be reset to no-rate-adjustment
  ValidateSyncResetSource(client_adjustable, device_monotonic);
}

// Validate AudioClock::SyncModeForClocks() combinations leading to SyncMode::ResetSourceClock
TEST_F(AudioClockTest, SyncModeResetDestClock) {
  auto device_monotonic =
      AudioClock::DeviceFixed(clock::CloneOfMonotonic(), AudioClock::kMonotonicDomain);

  auto client_adjustable = AudioClock::ClientAdjustable(clock::AdjustableCloneOfMonotonic());

  // If device is in MONOTONIC domain, adjustable client clock can be reset to no-rate-adjustment
  ValidateSyncResetDest(device_monotonic, client_adjustable);
}

// Validate AudioClock::SyncModeForClocks() combinations leading to SyncMode::AdjustSourceClock
TEST_F(AudioClockTest, SyncModeAdjustSourceClock) {
  auto client_adjustable = AudioClock::ClientAdjustable(clock::AdjustableCloneOfMonotonic());

  auto client_fixed = AudioClock::ClientFixed(clock::CloneOfMonotonic());
  auto client_adjustable2 = AudioClock::ClientAdjustable(clock::AdjustableCloneOfMonotonic());
  auto device_non_monotonic = AudioClock::DeviceFixed(clock::CloneOfMonotonic(), kCustomDomain);
  auto device_adjustable =
      AudioClock::DeviceAdjustable(clock::AdjustableCloneOfMonotonic(), kCustomDomain);

  // If a client adjustable clock is used, adjust it so that it matches the other clock.
  ValidateSyncAdjustSource(client_adjustable, client_fixed);
  ValidateSyncAdjustSource(client_adjustable, client_adjustable2);
  ValidateSyncAdjustSource(client_adjustable, device_non_monotonic);
  ValidateSyncAdjustSource(client_adjustable, device_adjustable);
}

// Validate AudioClock::SyncModeForClocks() combinations leading to SyncMode::AdjustSourceClock
TEST_F(AudioClockTest, SyncModeAdjustDestClock) {
  auto client_fixed = AudioClock::ClientFixed(clock::CloneOfMonotonic());
  auto device_non_monotonic = AudioClock::DeviceFixed(clock::CloneOfMonotonic(), kCustomDomain);
  auto device_adjustable =
      AudioClock::DeviceAdjustable(clock::AdjustableCloneOfMonotonic(), kCustomDomain);

  auto client_adjustable = AudioClock::ClientAdjustable(clock::AdjustableCloneOfMonotonic());

  // If a client adjustable clock is used, adjust it so that it matches the other clock.
  ValidateSyncAdjustDest(client_fixed, client_adjustable);
  ValidateSyncAdjustDest(device_non_monotonic, client_adjustable);
  ValidateSyncAdjustDest(device_adjustable, client_adjustable);
}

// Validate AudioClock::SyncModeForClocks() combinations leading to SyncMode::MicroSrc
TEST_F(AudioClockTest, SyncModeMicroSrc) {
  auto client_fixed = AudioClock::ClientFixed(clock::CloneOfMonotonic());
  auto client_fixed2 = AudioClock::ClientFixed(clock::CloneOfMonotonic());

  auto device_monotonic =
      AudioClock::DeviceFixed(clock::CloneOfMonotonic(), AudioClock::kMonotonicDomain);
  auto device_non_monotonic = AudioClock::DeviceFixed(clock::CloneOfMonotonic(), kCustomDomain);
  auto device_diff_domain = AudioClock::DeviceFixed(clock::CloneOfMonotonic(), kCustomDomain2);

  auto device_adjustable =
      AudioClock::DeviceAdjustable(clock::AdjustableCloneOfMonotonic(), kCustomDomain);
  auto device_adjustable_diff_domain =
      AudioClock::DeviceAdjustable(clock::AdjustableCloneOfMonotonic(), kCustomDomain2);

  // If neither is Flexible, and if the clock pair does not include both a adjustable device clock
  // and the software clock designated to control it, then reconcile them using micro-SRC.
  ValidateSyncMicroSrc(client_fixed, client_fixed2);
  ValidateSyncMicroSrc(client_fixed, device_monotonic);
  ValidateSyncMicroSrc(client_fixed, device_non_monotonic);
  ValidateSyncMicroSrc(client_fixed, device_adjustable);

  ValidateSyncMicroSrc(device_monotonic, client_fixed);
  ValidateSyncMicroSrc(device_monotonic, device_non_monotonic);
  ValidateSyncMicroSrc(device_monotonic, device_adjustable);

  ValidateSyncMicroSrc(device_non_monotonic, client_fixed);
  ValidateSyncMicroSrc(device_non_monotonic, device_monotonic);
  ValidateSyncMicroSrc(device_non_monotonic, device_diff_domain);
  ValidateSyncMicroSrc(device_non_monotonic, device_adjustable_diff_domain);

  ValidateSyncMicroSrc(device_adjustable, client_fixed);
  ValidateSyncMicroSrc(device_adjustable, device_monotonic);
  ValidateSyncMicroSrc(device_adjustable, device_diff_domain);
  ValidateSyncMicroSrc(device_adjustable, device_adjustable_diff_domain);
}

}  // namespace media::audio
