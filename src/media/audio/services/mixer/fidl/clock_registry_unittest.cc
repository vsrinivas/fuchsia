// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/clock_registry.h"

#include <gtest/gtest.h>

#include "src/media/audio/lib/clock/synthetic_clock_realm.h"

namespace media_audio {

TEST(ClockRegistryTest, AddFindDrop) {
  ClockRegistry registry;

  const auto kDomain = 42;
  const bool kAdjustable = true;

  auto realm = SyntheticClockRealm::Create();
  auto clock = realm->CreateClock("clock", kDomain, kAdjustable);
  auto handle = clock->DuplicateZxClockUnreadable();

  registry.Add(clock);

  // Find must return the same clock.
  {
    auto result = registry.Find(clock->koid());
    ASSERT_TRUE(result.is_ok()) << result.status_string();
    EXPECT_EQ(result.value().get(), clock.get());
  }

  // Again, passing a `zx::clock` with the same koid.
  {
    auto result = registry.Find(handle);
    ASSERT_TRUE(result.is_ok()) << result.status_string();
    EXPECT_EQ(result.value().get(), clock.get());
  }

  // After dropping the only strong reference, the clock is no longer found.
  clock = nullptr;
  {
    auto result = registry.Find(handle);
    ASSERT_TRUE(result.is_error());
    EXPECT_EQ(result.status_value(), ZX_ERR_NOT_FOUND);
  }
}

TEST(ClockRegistryTest, FindFailsOnInvalidHandle) {
  ClockRegistry registry;

  auto result = registry.Find(zx::clock());
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.status_value(), ZX_ERR_BAD_HANDLE);
}

TEST(ClockRegistryTest, FindFailsOnUnknownClock) {
  ClockRegistry registry;

  zx::clock handle;
  ASSERT_EQ(ZX_OK, zx::clock::create(
                       ZX_CLOCK_OPT_AUTO_START | ZX_CLOCK_OPT_MONOTONIC | ZX_CLOCK_OPT_CONTINUOUS,
                       nullptr, &handle));

  auto result = registry.Find(handle);
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.status_value(), ZX_ERR_NOT_FOUND);
}

}  // namespace media_audio
