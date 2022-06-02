// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/clock/synthetic_clock.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <string>

#include <gtest/gtest.h>

#include "src/media/audio/lib/clock/clone_mono.h"

using media::TimelineFunction;

namespace media_audio {
namespace {

constexpr auto kMonotonicDomain = Clock::kMonotonicDomain;
constexpr auto kExternalDomain = Clock::kExternalDomain;

TEST(SyntheticClockTest, CreateUnadjustable) {
  auto realm = SyntheticClockRealm::Create();
  realm->AdvanceTo(zx::time(100));

  auto clock = realm->CreateClock("clock", kMonotonicDomain, false);
  EXPECT_EQ(clock->name(), "clock");
  EXPECT_EQ(clock->domain(), kMonotonicDomain);
  EXPECT_FALSE(clock->adjustable());
  EXPECT_EQ(clock->now(), zx::time(100));
  EXPECT_EQ(clock->to_clock_mono(), TimelineFunction(0, 0, 1, 1));
  EXPECT_TRUE(clock->IdenticalToMonotonicClock());
}

TEST(SyntheticClockTest, CreateAdjustable) {
  auto realm = SyntheticClockRealm::Create();
  realm->AdvanceTo(zx::time(100));

  auto clock = realm->CreateClock("clock", kExternalDomain, true);
  EXPECT_EQ(clock->name(), "clock");
  EXPECT_EQ(clock->domain(), kExternalDomain);
  EXPECT_TRUE(clock->adjustable());
  EXPECT_EQ(clock->now(), zx::time(100));
  EXPECT_EQ(clock->to_clock_mono(), TimelineFunction(0, 0, 1, 1));
  EXPECT_TRUE(clock->IdenticalToMonotonicClock());
}

TEST(SyntheticClockTest, CreateNonMonotonic) {
  auto realm = SyntheticClockRealm::Create();
  realm->AdvanceTo(zx::time(100));

  auto mono_to_ref = TimelineFunction(50, 0, 2, 1);
  auto clock = realm->CreateClock("clock", kExternalDomain, false, mono_to_ref.Inverse());
  EXPECT_EQ(clock->name(), "clock");
  EXPECT_EQ(clock->now(), zx::time(mono_to_ref.Apply(100)));
  EXPECT_EQ(clock->to_clock_mono(), mono_to_ref.Inverse());
  EXPECT_FALSE(clock->IdenticalToMonotonicClock());
}

TEST(SyntheticClockTest, Koids) {
  // It's impossible to create duplicate synthetic clocks, so koids should never match.
  auto realm = SyntheticClockRealm::Create();
  auto clock1 = realm->CreateClock("clock1", kMonotonicDomain, false);
  auto clock2 = realm->CreateClock("clock2", kMonotonicDomain, false);
  EXPECT_NE(clock1->koid(), clock2->koid());
}

TEST(SyntheticClockTest, SetTimeAndRate) {
  auto m0 = zx::time(0);
  auto m1 = zx::time(0) + zx::sec(1);
  auto m2 = zx::time(0) + zx::sec(2);
  auto m3 = zx::time(0) + zx::sec(3);

  auto realm = SyntheticClockRealm::Create();
  auto clock = realm->CreateClock("clock", kExternalDomain, true);
  EXPECT_EQ(clock->now(), m0);
  EXPECT_TRUE(clock->IdenticalToMonotonicClock());
  EXPECT_EQ(clock->to_clock_mono_snapshot().generation, 0);

  realm->AdvanceTo(m1);
  EXPECT_EQ(clock->now(), m1);
  EXPECT_TRUE(clock->IdenticalToMonotonicClock());
  EXPECT_EQ(clock->to_clock_mono_snapshot().generation, 0);

  // Reference clock runs at 1.001x.
  clock->SetRate(1000);
  EXPECT_EQ(clock->to_clock_mono_snapshot().generation, 1);
  EXPECT_EQ(clock->to_clock_mono(), TimelineFunction(m1.get(), m1.get(), 1000, 1001));
  EXPECT_FALSE(clock->IdenticalToMonotonicClock());

  // Reference clock advances by 1.001 seconds over the next second.
  realm->AdvanceTo(m2);
  auto r2 = m2 + zx::sec(1) / 1000;
  EXPECT_EQ(clock->now(), r2);
  EXPECT_EQ(clock->ReferenceTimeFromMonotonicTime(m2), r2);
  EXPECT_EQ(clock->MonotonicTimeFromReferenceTime(r2), m2);

  // Reference clock runs at 0.999x.
  clock->SetRate(-1000);
  EXPECT_EQ(clock->to_clock_mono_snapshot().generation, 2);
  EXPECT_EQ(clock->to_clock_mono(), TimelineFunction(m2.get(), r2.get(), 1000, 999));
  EXPECT_FALSE(clock->IdenticalToMonotonicClock());

  // Reference clock advances by 0.999 seconds over the next second.
  // This brings the reference and monotonic clocks back in alignment.
  realm->AdvanceTo(m3);
  EXPECT_EQ(clock->now(), m3);
  EXPECT_EQ(clock->ReferenceTimeFromMonotonicTime(m3), m3);
  EXPECT_EQ(clock->MonotonicTimeFromReferenceTime(m3), m3);
}

TEST(SyntheticClockRealmTest, Advance) {
  auto realm = SyntheticClockRealm::Create();
  auto clock1 = realm->CreateClock("clock1", kExternalDomain, true);
  EXPECT_EQ(realm->now(), zx::time(0));
  EXPECT_EQ(clock1->now(), zx::time(0));

  realm->AdvanceBy(zx::nsec(10));
  auto clock2 = realm->CreateClock("clock2", kExternalDomain, true);
  EXPECT_EQ(realm->now(), zx::time(10));
  EXPECT_EQ(clock1->now(), zx::time(10));
  EXPECT_EQ(clock2->now(), zx::time(10));

  realm->AdvanceTo(zx::time(50));
  EXPECT_EQ(realm->now(), zx::time(50));
  EXPECT_EQ(clock1->now(), zx::time(50));
  EXPECT_EQ(clock2->now(), zx::time(50));
}

TEST(SyntheticClockRealmTest, DuplicateUnreadable) {
  auto realm = SyntheticClockRealm::Create();
  auto clock = realm->CreateClock("clock", kExternalDomain, true);

  auto zx_clock = clock->DuplicateZxClockUnreadable();

  // Must not have WRITE or READ.
  zx_info_handle_basic_t info;
  auto status = zx_clock.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  FX_CHECK(status == ZX_OK) << "zx_clock.get_info failed, status is " << status;
  EXPECT_EQ(info.rights, ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER);

  // Clock read should fail.
  zx_time_t unused;
  status = zx_clock.read(&unused);
  EXPECT_NE(status, ZX_OK);
}

}  // namespace
}  // namespace media_audio
