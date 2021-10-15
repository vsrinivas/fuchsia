// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/clock/audio_clock.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>
#include <zircon/syscalls/clock.h>

#include <string>

#include <gtest/gtest.h>

#include "lib/zx/time.h"
#include "src/media/audio/lib/clock/clone_mono.h"

namespace media::audio {

class AudioClockTest : public testing::Test {
 protected:
  static constexpr uint32_t kCustomDomain = 42;
  static constexpr uint32_t kCustomDomain2 = 68;

  static std::string ClockSummary(const AudioClock& clock) {
    std::string summary;
    summary = (clock.is_client_clock() ? "Client" : "Device");
    summary.append(clock.is_adjustable() ? "Adjustable" : "Fixed");
    auto ref_to_mono = clock.ref_clock_to_clock_mono();
    if (ref_to_mono.subject_delta() != ref_to_mono.reference_delta()) {
      summary.append(", rate-adjusted (" + std::to_string(ref_to_mono.subject_delta()) + "/" +
                     std::to_string(ref_to_mono.reference_delta()) + ")");
    } else if (ref_to_mono.subject_time() != ref_to_mono.reference_time()) {
      summary.append(", offset (" +
                     std::to_string(ref_to_mono.subject_time() - ref_to_mono.reference_time()) +
                     " ns)");
    } else {
      summary.append(", monotonic");
    }

    if (clock.is_device_clock()) {
      summary.append(", domain " + std::to_string(clock.domain()));
    }
    return summary;
  }

  void ExpectZeroMicroSrc(AudioClock& source_clock, AudioClock& dest_clock) {
    ExpectZeroMicroSrc(source_clock, dest_clock, true);
  }
  void ExpectNonzeroMicroSrc(AudioClock& source_clock, AudioClock& dest_clock) {
    ExpectZeroMicroSrc(source_clock, dest_clock, false);
  }
  void ExpectZeroMicroSrc(AudioClock& source_clock, AudioClock& dest_clock, bool expect_zero) {
    // Return these clocks to a known state.
    auto now = zx::clock::get_monotonic();
    source_clock.ResetRateAdjustment(now - zx::msec(10));
    dest_clock.ResetRateAdjustment(now - zx::msec(10));

    auto micro_src_ppm = AudioClock::SynchronizeClocks(source_clock, dest_clock, now, zx::usec(10));
    if (expect_zero) {
      EXPECT_EQ(0, micro_src_ppm);
    } else {
      EXPECT_GT(0, micro_src_ppm);
    }
  }

  void TestRevertToMonotonic(AudioClock source_clock, AudioClock dest_clock);
};

TEST_F(AudioClockTest, EqualsOperator) {
  auto clock1 = AudioClock::ClientAdjustable(clock::AdjustableCloneOfMonotonic());
  auto clock2 = AudioClock::ClientAdjustable(clock::AdjustableCloneOfMonotonic());
  EXPECT_FALSE(clock1 == clock2);

  auto result = clock1.DuplicateClock();
  ASSERT_TRUE(result.is_ok());

  auto clock3 = AudioClock::ClientFixed(result.take_value());
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

  auto dupe_raw_clock_result = audio_clock.DuplicateClock();
  ASSERT_TRUE(dupe_raw_clock_result.is_ok());
  EXPECT_TRUE(dupe_raw_clock_result.value().is_valid());

  auto dupe_audio_clock = AudioClock::ClientFixed(dupe_raw_clock_result.take_value());

  auto time1 = dupe_audio_clock.Read().get();
  auto time2 = dupe_audio_clock.Read().get();

  EXPECT_LT(time1, time2);
}

// Validate ClientAdjustable tuning behavior, when their counterpart clock is changed from
// non-monotonic to monotonic. This validates both RevertSource and RevertDest modes; one of the
// source and dest clocks is ClientAdjustable; the other is DeviceFixed monotonic.
//
// A very large initial error and a non-monotonic DeviceFixed counterpart leads the client clock
// to the sync mode RevertSource|DestToMonotonic, which converges rapidly to within 10 nsec.
void AudioClockTest::TestRevertToMonotonic(AudioClock source_clock, AudioClock dest_clock) {
  // This is our initial device clock before switching to the provided monotonic one.
  AudioClock device_diff_domain =
      AudioClock::DeviceFixed(clock::AdjustableCloneOfMonotonic(), kCustomDomain);

  bool client_is_source = source_clock.is_client_clock();
  AudioClock& client_clock = client_is_source ? source_clock : dest_clock;

  AudioClock& initial_source_clock = client_is_source ? client_clock : device_diff_domain;
  AudioClock& initial_dest_clock = client_is_source ? device_diff_domain : client_clock;

  // Position errors affect tuning in opposite manner, when tuning source clock versus dest clock.
  const auto kLargeError = zx::nsec(10000 * (client_is_source ? 1 : -1));
  const auto kSmallError = zx::nsec(50 * (client_is_source ? 1 : -1));
  const auto kVerySmallError = zx::nsec(5 * (client_is_source ? 1 : -1));

  auto now = zx::clock::get_monotonic();
  client_clock.ResetRateAdjustment(now);

  // This error should result in significant upward adjustment of the client clock.
  now += zx::msec(10);
  AudioClock::SynchronizeClocks(initial_source_clock, initial_dest_clock, now, kLargeError);
  auto mono_to_client_ref = client_clock.ref_clock_to_clock_mono().Inverse();
  EXPECT_GT(mono_to_client_ref.subject_delta(), mono_to_client_ref.reference_delta())
      << "sub_delta " << mono_to_client_ref.subject_delta() << ", ref_delta "
      << mono_to_client_ref.reference_delta();

  // Syncing now to a MONOTONIC device clock, this error is still too large for us to fine-tune the
  // client_clock toward perfect alignment, so PID-driven tuning continues.
  now += zx::msec(10);
  AudioClock::SynchronizeClocks(source_clock, dest_clock, now, kLargeError);
  mono_to_client_ref = client_clock.ref_clock_to_clock_mono().Inverse();
  EXPECT_GT(mono_to_client_ref.subject_delta(), mono_to_client_ref.reference_delta())
      << "sub_delta " << mono_to_client_ref.subject_delta() << ", ref_delta "
      << mono_to_client_ref.reference_delta();
  // The upward clock adjustment should be MUCH MORE than just 1 ppm.
  EXPECT_GT(mono_to_client_ref.rate().Scale(1'000'000), 1'000'001u);

  // Once the error is small enough, client-clock-tuning transitions to fine-tuning of +/- 1 ppm.
  now += zx::msec(10);
  AudioClock::SynchronizeClocks(source_clock, dest_clock, now, kSmallError);
  mono_to_client_ref = client_clock.ref_clock_to_clock_mono().Inverse();
  EXPECT_GE(mono_to_client_ref.rate().Scale(1'000'000), 1'000'001u);

  // And once error is very close to zero, client_clock should reset to no rate adjustment.
  now += zx::msec(10);
  AudioClock::SynchronizeClocks(source_clock, dest_clock, now, kVerySmallError);
  mono_to_client_ref = client_clock.ref_clock_to_clock_mono().Inverse();
  EXPECT_EQ(mono_to_client_ref.subject_delta(), mono_to_client_ref.reference_delta());

  now += zx::msec(10);
  AudioClock::SynchronizeClocks(source_clock, dest_clock, now, zx::nsec(0) - kVerySmallError);
  mono_to_client_ref = client_clock.ref_clock_to_clock_mono().Inverse();
  EXPECT_EQ(mono_to_client_ref.subject_delta(), mono_to_client_ref.reference_delta());
}

// Validate AudioClock::SyncMode::RevertSourceToMonotonic for ClientAdjustable clocks.
// This mode is triggered by a synchronization with a MONOTONIC device clock.
TEST_F(AudioClockTest, RevertSourceToMonotonicAdjustments) {
  TestRevertToMonotonic(
      AudioClock::ClientAdjustable(clock::AdjustableCloneOfMonotonic()),
      AudioClock::DeviceFixed(clock::CloneOfMonotonic(), AudioClock::kMonotonicDomain));
}

// Validate AudioClock::SyncMode::RevertDestToMonotonic for ClientAdjustable clocks.
// This mode is triggered by a synchronization with a MONOTONIC device clock.
TEST_F(AudioClockTest, RevertDestToMonotonicAdjustments) {
  TestRevertToMonotonic(
      AudioClock::DeviceFixed(clock::CloneOfMonotonic(), AudioClock::kMonotonicDomain),
      AudioClock::ClientAdjustable(clock::AdjustableCloneOfMonotonic()));
}

class AudioClockSyncModeTest : public AudioClockTest {
 protected:
  // Return a zx::clock running at the monotonic rate, but with some offset
  zx::clock MonotonicOffsetClock() {
    // Nudge rate away from 0 then back to o.
    auto zx_clock = clock::AdjustableCloneOfMonotonic();
    zx::clock::update_args args;
    args.reset().set_rate_adjust(1000);
    EXPECT_TRUE(zx_clock.update(args) == ZX_OK) << "First clock rate_adjust failed";

    zx::nanosleep(zx::deadline_after(zx::msec(2)));
    args.set_rate_adjust(0);
    EXPECT_TRUE(zx_clock.update(args) == ZX_OK) << "Second clock rate_adjust failed";

    return zx_clock;
  }

  // Convenience methods so AudioClock only has to declare one class (AudioClockTest) as friend.
  //
  // These clocks require no synchronization; they are guaranteed to currently be in sync.
  void ValidateSyncNone(AudioClock& source_clock, AudioClock& dest_clock) {
    SCOPED_TRACE("SyncMode::None, Source " + ClockSummary(source_clock) + ", Dest " +
                 ClockSummary(dest_clock));
    EXPECT_EQ(AudioClock::SyncMode::None, AudioClock::SyncModeForClocks(source_clock, dest_clock));

    EXPECT_TRUE(AudioClock::NoSynchronizationRequired(source_clock, dest_clock));
    ExpectZeroMicroSrc(source_clock, dest_clock);

    // No synch is required right now, but a ClientFixed's owner can rate-adjust it at any time
    // (requiring us to compensate with micro-SRC).
    if (source_clock == dest_clock) {
      // Even if this clock is adjusted, it will always be in sync with itself!
      EXPECT_FALSE(AudioClock::SynchronizationNeedsHighQualityResampler(source_clock, dest_clock));
    } else if ((source_clock.is_client_clock() && !source_clock.is_adjustable()) ||
               (dest_clock.is_client_clock() && !dest_clock.is_adjustable())) {
      // One of the clocks can be externally rate-controlled; plan ahead by using high-quality SRC.
      EXPECT_TRUE(AudioClock::SynchronizationNeedsHighQualityResampler(source_clock, dest_clock));
    } else {
      // Neither clock can be adjusted "from the outside"; we'll never need to micro-SRC.
      EXPECT_FALSE(AudioClock::SynchronizationNeedsHighQualityResampler(source_clock, dest_clock));
    }
  }

  // These clocks can be reconciled by adjusting the source clock until it is synchronized with the
  // destination clock, then rate-locking the source clock to the CLOCK_MONOTONIC rate.
  void ValidateSyncRevertSource(AudioClock& source_clock, AudioClock& dest_clock) {
    SCOPED_TRACE("SyncMode::RevertSourceToMonotonic, Source " + ClockSummary(source_clock) +
                 ", Dest " + ClockSummary(dest_clock));
    EXPECT_EQ(AudioClock::SyncMode::RevertSourceToMonotonic,
              AudioClock::SyncModeForClocks(source_clock, dest_clock));

    ExpectZeroMicroSrc(source_clock, dest_clock);
    EXPECT_FALSE(AudioClock::NoSynchronizationRequired(source_clock, dest_clock));
    EXPECT_FALSE(AudioClock::SynchronizationNeedsHighQualityResampler(source_clock, dest_clock));
  }

  // These clocks can be reconciled by adjusting the destination clock until it is synchronized with
  // the source clock, then rate-locking the destination clock to the CLOCK_MONOTONIC rate.
  void ValidateSyncRevertDest(AudioClock& source_clock, AudioClock& dest_clock) {
    SCOPED_TRACE("SyncMode::RevertDestToMonotonic, Source " + ClockSummary(source_clock) +
                 ", Dest " + ClockSummary(dest_clock));
    EXPECT_EQ(AudioClock::SyncMode::RevertDestToMonotonic,
              AudioClock::SyncModeForClocks(source_clock, dest_clock));

    ExpectZeroMicroSrc(source_clock, dest_clock);
    EXPECT_FALSE(AudioClock::NoSynchronizationRequired(source_clock, dest_clock));
    EXPECT_FALSE(AudioClock::SynchronizationNeedsHighQualityResampler(source_clock, dest_clock));
  }

  // These clocks can be reconciled by continually adjusting the source clock.
  void ValidateSyncAdjustSource(AudioClock& source_clock, AudioClock& dest_clock) {
    SCOPED_TRACE("SyncMode::AdjustSourceClock, Source " + ClockSummary(source_clock) + ", Dest " +
                 ClockSummary(dest_clock));
    EXPECT_EQ(AudioClock::SyncMode::AdjustSourceClock,
              AudioClock::SyncModeForClocks(source_clock, dest_clock));

    ExpectZeroMicroSrc(source_clock, dest_clock);
    EXPECT_FALSE(AudioClock::NoSynchronizationRequired(source_clock, dest_clock));
    EXPECT_FALSE(AudioClock::SynchronizationNeedsHighQualityResampler(source_clock, dest_clock));
  }

  // These clocks can be reconciled by continually adjusting the destination clock.
  void ValidateSyncAdjustDest(AudioClock& source_clock, AudioClock& dest_clock) {
    SCOPED_TRACE("SyncMode::AdjustDestClock, Source " + ClockSummary(source_clock) + ", Dest " +
                 ClockSummary(dest_clock));
    EXPECT_EQ(AudioClock::SyncMode::AdjustDestClock,
              AudioClock::SyncModeForClocks(source_clock, dest_clock));

    ExpectZeroMicroSrc(source_clock, dest_clock);
    EXPECT_FALSE(AudioClock::NoSynchronizationRequired(source_clock, dest_clock));
    EXPECT_FALSE(AudioClock::SynchronizationNeedsHighQualityResampler(source_clock, dest_clock));
  }

  // These clocks can only be reconciled with ongoing micro-SRC.
  void ValidateSyncMicroSrc(AudioClock& source_clock, AudioClock& dest_clock) {
    SCOPED_TRACE("SyncMode::MicroSrc, Source " + ClockSummary(source_clock) + ", Dest " +
                 ClockSummary(dest_clock));
    EXPECT_EQ(AudioClock::SyncMode::MicroSrc,
              AudioClock::SyncModeForClocks(source_clock, dest_clock));

    if (source_clock.is_client_clock() || dest_clock.is_client_clock()) {
      ExpectNonzeroMicroSrc(source_clock, dest_clock);
    }
    EXPECT_FALSE(AudioClock::NoSynchronizationRequired(source_clock, dest_clock));
    EXPECT_TRUE(AudioClock::SynchronizationNeedsHighQualityResampler(source_clock, dest_clock));
  }

  AudioClock client_adjustable_monotonic_ =
      AudioClock::ClientAdjustable(clock::AdjustableCloneOfMonotonic());
  AudioClock client_adjustable_offset_ = AudioClock::ClientAdjustable(MonotonicOffsetClock());
  AudioClock client_fixed_monotonic_ = AudioClock::ClientFixed(clock::CloneOfMonotonic());
  AudioClock client_fixed_offset_ = AudioClock::ClientFixed(MonotonicOffsetClock());

  AudioClock device_monotonic_ =
      AudioClock::DeviceFixed(clock::CloneOfMonotonic(), AudioClock::kMonotonicDomain);
  AudioClock other_device_monotonic_ =
      AudioClock::DeviceFixed(clock::CloneOfMonotonic(), AudioClock::kMonotonicDomain);

  AudioClock device_adjustable_domain_1_ =
      AudioClock::DeviceAdjustable(clock::AdjustableCloneOfMonotonic(), kCustomDomain);
  AudioClock other_device_adjustable_domain_1_ =
      AudioClock::DeviceAdjustable(clock::AdjustableCloneOfMonotonic(), kCustomDomain);
  AudioClock device_adjustable_domain_2_ =
      AudioClock::DeviceAdjustable(clock::AdjustableCloneOfMonotonic(), kCustomDomain2);

  AudioClock device_domain_1_ = AudioClock::DeviceFixed(clock::CloneOfMonotonic(), kCustomDomain);
  AudioClock other_device_domain_1_ =
      AudioClock::DeviceFixed(clock::CloneOfMonotonic(), kCustomDomain);
  AudioClock device_domain_2_ = AudioClock::DeviceFixed(clock::CloneOfMonotonic(), kCustomDomain2);
};

// Validate AudioClock::SyncModeForClocks() combinations leading to SyncMode::None
TEST_F(AudioClockSyncModeTest, SyncModeNone) {
  // No synchronization is needed, when reconciling any clock with itself.
  ValidateSyncNone(client_adjustable_offset_, client_adjustable_offset_);
  ValidateSyncNone(client_fixed_monotonic_, client_fixed_monotonic_);
  ValidateSyncNone(client_fixed_offset_, client_fixed_offset_);
  ValidateSyncNone(device_monotonic_, device_monotonic_);
  ValidateSyncNone(device_domain_1_, device_domain_1_);
  ValidateSyncNone(device_adjustable_domain_1_, device_adjustable_domain_1_);
  // This includes if two AudioClock objects have the same underlying koid
  ValidateSyncNone(device_monotonic_, other_device_monotonic_);

  // No synchronization is needed, when reconciling two device clocks in the same domain.
  ValidateSyncNone(device_adjustable_domain_1_, other_device_adjustable_domain_1_);
  ValidateSyncNone(device_adjustable_domain_1_, device_domain_1_);
  ValidateSyncNone(device_domain_1_, device_adjustable_domain_1_);
  ValidateSyncNone(device_domain_1_, other_device_domain_1_);

  // No synchronization is needed, if device clock is in monotonic domain and client clock is a
  // clone of monotonic and has never been adjusted.
  ValidateSyncNone(client_adjustable_monotonic_, device_monotonic_);
  ValidateSyncNone(client_fixed_monotonic_, device_monotonic_);
  ValidateSyncNone(device_monotonic_, client_adjustable_monotonic_);
  ValidateSyncNone(device_monotonic_, client_fixed_monotonic_);
}

// Validate AudioClock::SyncModeForClocks() combinations leading to
// SyncMode::RevertSourceToMonotonic
TEST_F(AudioClockSyncModeTest, SyncModeRevertSourceToMonotonic) {
  // If device (destination) clock is in MONOTONIC domain, then an adjustable client (source) clock
  // can be reverted to no-rate-adjustment
  ValidateSyncRevertSource(client_adjustable_offset_, device_monotonic_);
}

// Validate AudioClock::SyncModeForClocks() combinations leading to
// SyncMode::RevertSourceToMonotonic
TEST_F(AudioClockSyncModeTest, SyncModeRevertDestToMonotonic) {
  // If device (source) clock is in MONOTONIC domain, then an adjustable client (destination) clock
  // can be reverted to no-rate-adjustment
  ValidateSyncRevertDest(device_monotonic_, client_adjustable_offset_);
}

// Validate AudioClock::SyncModeForClocks() combinations leading to SyncMode::AdjustSourceClock
TEST_F(AudioClockSyncModeTest, SyncModeAdjustSourceClock) {
  // If a client adjustable clock is used, adjust it so that it matches the other clock.
  ValidateSyncAdjustSource(client_adjustable_offset_, client_fixed_monotonic_);
  ValidateSyncAdjustSource(client_adjustable_offset_, client_adjustable_monotonic_);
  ValidateSyncAdjustSource(client_adjustable_offset_, device_domain_1_);
  ValidateSyncAdjustSource(client_adjustable_offset_, device_adjustable_domain_1_);
}

// Validate AudioClock::SyncModeForClocks() combinations leading to SyncMode::AdjustSourceClock
TEST_F(AudioClockSyncModeTest, SyncModeAdjustDestClock) {
  // If a client adjustable clock is used, adjust it so that it matches the other clock.
  ValidateSyncAdjustDest(client_fixed_monotonic_, client_adjustable_offset_);
  ValidateSyncAdjustDest(device_adjustable_domain_1_, client_adjustable_offset_);
  ValidateSyncAdjustDest(device_domain_1_, client_adjustable_offset_);
}

// Validate AudioClock::SyncModeForClocks() combinations leading to SyncMode::MicroSrc
TEST_F(AudioClockSyncModeTest, SyncModeMicroSrc) {
  // If neither is Flexible, and if the clock pair does not include both a adjustable device clock
  // and the software clock designated to control it, then reconcile them using micro-SRC.
  ValidateSyncMicroSrc(client_fixed_monotonic_, device_adjustable_domain_1_);
  ValidateSyncMicroSrc(client_fixed_monotonic_, device_domain_1_);
  ValidateSyncMicroSrc(client_fixed_offset_, client_fixed_monotonic_);
  ValidateSyncMicroSrc(client_fixed_offset_, device_monotonic_);

  ValidateSyncMicroSrc(device_monotonic_, client_fixed_offset_);
  ValidateSyncMicroSrc(device_monotonic_, device_domain_1_);
  ValidateSyncMicroSrc(device_monotonic_, device_adjustable_domain_1_);

  ValidateSyncMicroSrc(device_domain_1_, client_fixed_monotonic_);
  ValidateSyncMicroSrc(device_domain_1_, device_monotonic_);
  ValidateSyncMicroSrc(device_domain_1_, device_domain_2_);
  ValidateSyncMicroSrc(device_domain_1_, device_adjustable_domain_2_);

  ValidateSyncMicroSrc(device_adjustable_domain_1_, client_fixed_monotonic_);
  ValidateSyncMicroSrc(device_adjustable_domain_1_, device_monotonic_);
  ValidateSyncMicroSrc(device_adjustable_domain_1_, device_domain_2_);
  ValidateSyncMicroSrc(device_adjustable_domain_1_, device_adjustable_domain_2_);
}

}  // namespace media::audio
