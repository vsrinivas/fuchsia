// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/clock/clock_synchronizer.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <string>

#include <gtest/gtest.h>

#include "src/media/audio/lib/clock/synthetic_clock_realm.h"

namespace media_audio {
namespace {

using ::media::TimelineFunction;

constexpr uint32_t kMonotonicDomain = Clock::kMonotonicDomain;
constexpr uint32_t kExternalDomain = Clock::kExternalDomain;
constexpr uint32_t kCustomDomain = 42;

// Arbitrary initial `to_clock_mono` transform used by kCustomDomain to ensure
// that clocks in this domain don't match kMonotonicDomain.
const TimelineFunction kCustomDomainInitialToMono(0, 0, 100, 101);

const std::vector<ClockSynchronizer::Mode> kAllModes{
    ClockSynchronizer::Mode::WithAdjustments,
    ClockSynchronizer::Mode::WithMicroSRC,
};

std::string ModeToString(ClockSynchronizer::Mode mode) {
  return mode == ClockSynchronizer::Mode::WithAdjustments ? "WithAdjustments" : "WithMicroSRC";
}

void TestNoChange(std::shared_ptr<ClockSynchronizer> sync, std::shared_ptr<Clock> follower) {
  sync->Reset(zx::time(0));
  auto follower_to_mono_before = follower->to_clock_mono();

  // In all cases where we expect no change, `follower_pos_error` should be zero.
  sync->Update(zx::time(10), zx::nsec(0));
  EXPECT_EQ(sync->follower_adjustment_ppm(), 0);

  auto follower_to_mono_after = follower->to_clock_mono();
  EXPECT_EQ(follower_to_mono_before, follower_to_mono_after);
}

TEST(ClockSynchronizerTest, SelectModeBothNotAdjustable) {
  // Neither is adjustable, so we must use MicroSRC.
  auto realm = SyntheticClockRealm::Create();
  auto source = realm->CreateClock("source", kExternalDomain, false);
  auto dest = realm->CreateClock("dest", kExternalDomain, false);
  auto sync = ClockSynchronizer::SelectModeAndCreate(source, dest);
  EXPECT_EQ(sync->mode(), ClockSynchronizer::Mode::WithMicroSRC);
  EXPECT_EQ(sync->leader(), dest);
  EXPECT_EQ(sync->follower(), source);
}

TEST(ClockSynchronizerTest, SelectModeSourceAdjustable) {
  // Neither is adjustable, so we must use MicroSRC.
  auto realm = SyntheticClockRealm::Create();
  auto source = realm->CreateClock("source", kExternalDomain, true);
  auto dest = realm->CreateClock("dest", kExternalDomain, false);
  auto sync = ClockSynchronizer::SelectModeAndCreate(source, dest);
  EXPECT_EQ(sync->mode(), ClockSynchronizer::Mode::WithAdjustments);
  EXPECT_EQ(sync->leader(), dest);
  EXPECT_EQ(sync->follower(), source);
}

TEST(ClockSynchronizerTest, SelectModeDestAdjustable) {
  // Neither is adjustable, so we must use MicroSRC.
  auto realm = SyntheticClockRealm::Create();
  auto source = realm->CreateClock("source", kExternalDomain, false);
  auto dest = realm->CreateClock("dest", kExternalDomain, true);
  auto sync = ClockSynchronizer::SelectModeAndCreate(source, dest);
  EXPECT_EQ(sync->mode(), ClockSynchronizer::Mode::WithAdjustments);
  EXPECT_EQ(sync->leader(), source);
  EXPECT_EQ(sync->follower(), dest);
}

TEST(ClockSynchronizerTest, SameClocks) {
  for (auto mode : kAllModes) {
    SCOPED_TRACE(ModeToString(mode));

    // Same clock for the leader and the follower.
    auto realm = SyntheticClockRealm::Create();
    auto clock = realm->CreateClock("clock", kExternalDomain, true);
    auto sync = ClockSynchronizer::Create(clock, clock, mode);
    TestNoChange(sync, clock);
  }
}

TEST(ClockSynchronizerTest, SameDomain) {
  for (auto mode : kAllModes) {
    SCOPED_TRACE(ModeToString(mode));

    // These two clocks are in the same domain (hence have the same rate) but are relatively offset.
    auto realm = SyntheticClockRealm::Create();
    auto leader = realm->CreateClock("lead", kCustomDomain, false, TimelineFunction(2, 0, 100, 99));
    auto follower =
        realm->CreateClock("follow", kCustomDomain, true, TimelineFunction(1, 0, 100, 99));
    auto sync = ClockSynchronizer::Create(leader, follower, mode);
    TestNoChange(sync, follower);
  }
}

TEST(ClockSynchronizerTest, FollowerNotAdjustedYet) {
  for (auto mode : kAllModes) {
    SCOPED_TRACE(ModeToString(mode));

    // Leader is in the monotonic domain.
    // Follower starts identical to monotonic and hasn't been updated yet.
    auto realm = SyntheticClockRealm::Create();
    auto leader = realm->CreateClock("lead", kMonotonicDomain, false);
    auto follower = realm->CreateClock("follow", kExternalDomain, true);
    auto sync = ClockSynchronizer::Create(leader, follower, mode);
    TestNoChange(sync, follower);
  }
}

TEST(ClockSynchronizerTest, RevertToMonotonic) {
  const auto kLargeError = zx::nsec(10000);
  const auto kSmallError = zx::nsec(50);
  const auto kVerySmallError = zx::nsec(5);

  auto realm = SyntheticClockRealm::Create();
  auto follower = realm->CreateClock("follower", kExternalDomain, true);

  // Do one round synchronized to a clock in kCustomDomain.
  {
    auto leader = realm->CreateClock("leader0", kCustomDomain, false, kCustomDomainInitialToMono);
    auto sync =
        ClockSynchronizer::Create(leader, follower, ClockSynchronizer::Mode::WithAdjustments);
    sync->Reset(realm->now());

    // This error should result in significant upward adjustment of the client clock.
    realm->AdvanceBy(zx::msec(10));
    sync->Update(realm->now(), kLargeError);
    auto mono_to_follower_ref = follower->to_clock_mono().Inverse();
    EXPECT_GT(sync->follower_adjustment_ppm(), 0);
    EXPECT_GT(mono_to_follower_ref.subject_delta(), mono_to_follower_ref.reference_delta())
        << "sub_delta " << mono_to_follower_ref.subject_delta() << ", ref_delta "
        << mono_to_follower_ref.reference_delta();
  }

  // Now synchronize to a clock in kMonotonicDomain.
  auto leader = realm->CreateClock("leader0", kMonotonicDomain, false);
  auto sync = ClockSynchronizer::Create(leader, follower, ClockSynchronizer::Mode::WithAdjustments);

  // Syncing now to a MONOTONIC device clock, this error is still too large for us to fine-tune the
  // follower toward perfect alignment, so PID-driven tuning continues.
  sync->Reset(realm->now());
  realm->AdvanceBy(zx::msec(10));
  sync->Update(realm->now(), kLargeError);
  auto mono_to_follower_ref = follower->to_clock_mono().Inverse();
  EXPECT_GT(sync->follower_adjustment_ppm(), 0);
  EXPECT_GT(mono_to_follower_ref.subject_delta(), mono_to_follower_ref.reference_delta())
      << "sub_delta " << mono_to_follower_ref.subject_delta() << ", ref_delta "
      << mono_to_follower_ref.reference_delta();
  // The upward clock adjustment should be MUCH MORE than just 1 ppm.
  EXPECT_GT(mono_to_follower_ref.rate().Scale(1'000'000), 1'000'001u);

  // Once the error is small enough, follower-clock-tuning transitions to fine-tuning of +/- 1 ppm.
  realm->AdvanceBy(zx::msec(10));
  sync->Update(realm->now(), kSmallError);
  mono_to_follower_ref = follower->to_clock_mono().Inverse();
  EXPECT_GE(mono_to_follower_ref.rate().Scale(1'000'000), 1'000'001u);
  EXPECT_GE(sync->follower_adjustment_ppm(), 1);

  // And once error is very close to zero, follower should reset to no rate adjustment.
  realm->AdvanceBy(zx::msec(10));
  sync->Update(realm->now(), kVerySmallError);
  mono_to_follower_ref = follower->to_clock_mono().Inverse();
  EXPECT_EQ(mono_to_follower_ref.subject_delta(), mono_to_follower_ref.reference_delta());
  EXPECT_EQ(sync->follower_adjustment_ppm(), 0);

  realm->AdvanceBy(zx::msec(10));
  sync->Update(realm->now(), zx::nsec(0) - kVerySmallError);
  mono_to_follower_ref = follower->to_clock_mono().Inverse();
  EXPECT_EQ(mono_to_follower_ref.subject_delta(), mono_to_follower_ref.reference_delta());
  EXPECT_EQ(sync->follower_adjustment_ppm(), 0);
}

TEST(ClockSynchronizerTest, Update) {
  for (auto mode : kAllModes) {
    SCOPED_TRACE(ModeToString(mode));

    // Follower is initially monotonic, running faster than the leader.
    auto realm = SyntheticClockRealm::Create();
    auto leader =
        realm->CreateClock("lead", kExternalDomain, false, TimelineFunction(10, 0, 101, 100));
    auto follower = realm->CreateClock("follow", kExternalDomain, true);
    auto sync = ClockSynchronizer::Create(leader, follower, mode);
    sync->Reset(realm->now());

    // After 100ms, the follower is 1ms ahead.
    realm->AdvanceBy(zx::msec(100));
    sync->Update(realm->now(), zx::msec(1));

    // Since the follower clock was ahead of the leader, it should have slowed down.
    if (mode == ClockSynchronizer::Mode::WithAdjustments) {
      // See audio_clock_coefficients.h for an explanation of why this branch is invered.
      EXPECT_GT(sync->follower_adjustment_ppm(), 0);
    } else {
      EXPECT_LT(sync->follower_adjustment_ppm(), 0);
    }

    // In MicroSRC mode, the follower's clock should never change.
    auto follower_to_mono = follower->to_clock_mono();
    if (mode == ClockSynchronizer::Mode::WithMicroSRC) {
      EXPECT_EQ(follower_to_mono.subject_delta(), follower_to_mono.reference_delta());
    } else {
      EXPECT_LT(follower_to_mono.subject_delta(), follower_to_mono.reference_delta());
    }
  }
}

}  // namespace
}  // namespace media_audio
