// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/clock/recovered_clock.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <string>

#include <gtest/gtest.h>

#include "src/media/audio/lib/clock/synthetic_clock.h"

using media::TimelineFunction;

namespace media_audio {

namespace {
constexpr auto kExternalDomain = Clock::kExternalDomain;
}  // namespace

TEST(RecoveredClockTest, Tune) {
  // Each increment in position is 10ns.
  TimelineFunction pos_to_ref_time(0, 0, zx::nsec(10).get(), 1);

  // Use a simple PID that uses P only.
  // Each 1ns worth of error contributes 1PPM to the PID.
  media::audio::clock::PidControl::Coefficients pid_coefficients{
      .proportional_factor = 0.000001,
      .integral_factor = 0,
      .derivative_factor = 0,
  };

  auto realm = SyntheticClockRealm::Create();
  auto backing = realm->CreateClock("backing", kExternalDomain, true);
  EXPECT_EQ(backing->name(), "backing");
  EXPECT_EQ(backing->domain(), kExternalDomain);
  EXPECT_TRUE(backing->adjustable());

  auto recovered = RecoveredClock::Create("recovered", backing, pid_coefficients);
  recovered->Reset(zx::time(0), pos_to_ref_time);
  EXPECT_EQ(recovered->name(), "recovered");
  EXPECT_EQ(recovered->koid(), backing->koid());
  EXPECT_EQ(recovered->domain(), kExternalDomain);
  EXPECT_FALSE(recovered->adjustable());
  EXPECT_TRUE(recovered->IdenticalToMonotonicClock());

  // After 20ns, the position should have advanced by 2.
  // Report that we saw an increment of just 1, which means the recovered clock should be
  // running at half of the monotonic rate. This gives an error of 10ns, which the PID should
  // translate to an adjusted rate of -10PPM.
  realm->AdvanceTo(zx::time(20));
  recovered->Update(realm->now(), 1);
  EXPECT_EQ(recovered->to_clock_mono(), TimelineFunction(20, 20, 1'000'000, 999'990));
  EXPECT_EQ(recovered->to_clock_mono(), backing->to_clock_mono());
}

}  // namespace media_audio
