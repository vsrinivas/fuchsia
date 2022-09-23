// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/clock/real_clock.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <zircon/syscalls/clock.h>

#include <string>

#include <gtest/gtest.h>

#include "src/media/audio/lib/clock/clone_mono.h"

namespace media_audio {
namespace {

constexpr auto kMonotonicDomain = Clock::kMonotonicDomain;
constexpr auto kExternalDomain = Clock::kExternalDomain;

zx::clock NewClock(zx_rights_t rights = ZX_RIGHT_DUPLICATE | ZX_RIGHT_READ | ZX_RIGHT_WRITE) {
  zx::clock clock;
  auto status = zx::clock::create(
      ZX_CLOCK_OPT_AUTO_START | ZX_CLOCK_OPT_MONOTONIC | ZX_CLOCK_OPT_CONTINUOUS, nullptr, &clock);
  FX_CHECK(status == ZX_OK) << "clock.create failed, status is " << status;

  status = clock.replace(rights, &clock);
  FX_CHECK(status == ZX_OK) << "clock.replace failed, status is " << status;

  return clock;
}

zx::clock DupClock(const zx::clock& in_clock, zx_rights_t rights = ZX_RIGHT_SAME_RIGHTS) {
  zx::clock out_clock;
  auto status = in_clock.duplicate(rights, &out_clock);
  FX_CHECK(status == ZX_OK) << "clock.duplicate failed, status is " << status;
  return out_clock;
}

TEST(RealClockTest, CreateUnadjustable) {
  const zx_rights_t rights = ZX_RIGHT_DUPLICATE | ZX_RIGHT_READ;
  auto clock = RealClock::Create("clock", NewClock(rights), kMonotonicDomain, false);
  EXPECT_EQ(clock->name(), "clock");
  EXPECT_EQ(clock->domain(), kMonotonicDomain);
  EXPECT_FALSE(clock->adjustable());
}

TEST(RealClockTest, CreateAdjustable) {
  const zx_rights_t rights = ZX_RIGHT_DUPLICATE | ZX_RIGHT_READ | ZX_RIGHT_WRITE;
  auto clock = RealClock::Create("clock", NewClock(rights), kExternalDomain, true);
  EXPECT_EQ(clock->name(), "clock");
  EXPECT_EQ(clock->domain(), kExternalDomain);
  EXPECT_TRUE(clock->adjustable());
}

TEST(RealClockTest, CreateUnadjustableMonotonic) {
  auto clock = RealClock::CreateFromMonotonic("clock", kMonotonicDomain, false);
  EXPECT_EQ(clock->name(), "clock");
  EXPECT_EQ(clock->domain(), kMonotonicDomain);
  EXPECT_FALSE(clock->adjustable());
  EXPECT_TRUE(clock->IdenticalToMonotonicClock());
}

TEST(RealClockTest, CreateAdjustableMonotonic) {
  auto clock = RealClock::CreateFromMonotonic("clock", kExternalDomain, true);
  EXPECT_EQ(clock->name(), "clock");
  EXPECT_EQ(clock->domain(), kExternalDomain);
  EXPECT_TRUE(clock->adjustable());
  EXPECT_TRUE(clock->IdenticalToMonotonicClock());
}

TEST(RealClockTest, Koids) {
  auto c1 = NewClock();
  auto c2 = DupClock(c1);
  auto c3 = NewClock();

  // Koids should match for duplicated clocks.
  auto clock1 = RealClock::Create("clock1", std::move(c1), kMonotonicDomain, false);
  auto clock2 = RealClock::Create("clock2", std::move(c2), kMonotonicDomain, false);
  auto clock3 = RealClock::Create("clock3", std::move(c3), kMonotonicDomain, false);

  EXPECT_EQ(clock1->koid(), clock2->koid());
  EXPECT_NE(clock1->koid(), clock3->koid());
}

TEST(RealClockTest, SetRate) {
  auto clock = RealClock::Create("clock", NewClock(), kExternalDomain, true);
  auto initial_snapshot = clock->to_clock_mono_snapshot();
  auto r1 = zx::time(0) + zx::hour(1);
  auto r2 = zx::time(0) + zx::hour(2);

  // Initially should be identical to system monotonic.
  EXPECT_TRUE(clock->IdenticalToMonotonicClock());
  EXPECT_EQ(clock->ReferenceTimeFromMonotonicTime(r1), r1);

  // Set the rate slower.
  clock->SetRate(-1000);
  {
    auto snapshot = clock->to_clock_mono_snapshot();
    EXPECT_EQ(initial_snapshot.generation + 1, snapshot.generation);

    auto to_mono = snapshot.to_clock_mono;
    EXPECT_LT(to_mono.reference_delta(), to_mono.subject_delta()) << "rate should be less than 1:1";

    auto m1 = clock->MonotonicTimeFromReferenceTime(r1);
    auto m2 = clock->MonotonicTimeFromReferenceTime(r2);
    EXPECT_LT(r2 - r1, m2 - m1);
    EXPECT_FALSE(clock->IdenticalToMonotonicClock());

    // This might be off by +/-1 due to rounding.
    auto diff = r1 - clock->ReferenceTimeFromMonotonicTime(m1);
    EXPECT_LE(std::abs(diff.get()), 1) << diff.get();
  }

  // Set the rate faster.
  clock->SetRate(1000);
  {
    auto snapshot = clock->to_clock_mono_snapshot();
    EXPECT_EQ(initial_snapshot.generation + 2, snapshot.generation);

    auto to_mono = snapshot.to_clock_mono;
    EXPECT_GT(to_mono.reference_delta(), to_mono.subject_delta()) << "rate should be more than 1:1";

    auto m1 = clock->MonotonicTimeFromReferenceTime(r1);
    auto m2 = clock->MonotonicTimeFromReferenceTime(r2);
    EXPECT_GT(r2 - r1, m2 - m1);
    EXPECT_FALSE(clock->IdenticalToMonotonicClock());

    // This might be off by +/-1 due to rounding.
    auto diff = r1 - clock->ReferenceTimeFromMonotonicTime(m1);
    EXPECT_LE(std::abs(diff.get()), 1) << diff.get();
  }
}

}  // namespace
}  // namespace media_audio
