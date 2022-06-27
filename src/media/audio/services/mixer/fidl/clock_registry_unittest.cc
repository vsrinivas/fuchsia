// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/clock_registry.h"

#include <gtest/gtest.h>

#include "src/media/audio/services/mixer/fidl/real_clock_factory.h"
#include "src/media/audio/services/mixer/fidl/synthetic_clock_factory.h"

namespace media_audio {

TEST(ClockRegistryTest, GraphControlledClocks) {
  auto realm = SyntheticClockRealm::Create();
  ClockRegistry registry(std::make_shared<SyntheticClockFactory>(realm));

  std::shared_ptr<Clock> clock1;
  zx::clock handle1;

  {
    auto result = registry.CreateGraphControlledClock();
    ASSERT_TRUE(result.is_ok()) << result.status_string();

    clock1 = std::move(result.value().first);
    handle1 = std::move(result.value().second);
  }

  // Check properties of the returned clock.
  zx_info_handle_basic_t info;
  ASSERT_EQ(ZX_OK, handle1.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));

  ASSERT_NE(clock1.get(), nullptr);
  EXPECT_EQ(clock1->name(), "GraphControlledClock0");
  EXPECT_EQ(clock1->domain(), Clock::kExternalDomain);
  EXPECT_EQ(clock1->koid(), info.koid);
  EXPECT_TRUE(clock1->adjustable());

  // The handle must duplicable and transferrable but not be writable.
  EXPECT_NE(info.rights & ZX_RIGHT_DUPLICATE, 0u);
  EXPECT_NE(info.rights & ZX_RIGHT_TRANSFER, 0u);
  EXPECT_EQ(info.rights & ZX_RIGHT_WRITE, 0u);

  // Must find this clock.
  {
    auto result = registry.FindClock(handle1);
    ASSERT_TRUE(result.is_ok()) << result.status_string();
    ASSERT_EQ(result.value().get(), clock1.get());
  }

  // After forgetting the clock, we can't find it.
  {
    auto result = registry.ForgetClock(handle1);
    ASSERT_TRUE(result.is_ok()) << result.status_string();
  }

  {
    auto result = registry.FindClock(handle1);
    ASSERT_FALSE(result.is_ok());
    EXPECT_EQ(result.status_value(), ZX_ERR_NOT_FOUND);
  }
}

TEST(ClockRegistryTest, UserControlledClocks) {
  // This test needs to use RealClockFactory because SyntheticClockFactory doesn't support
  // CreateWrappedClock.
  ClockRegistry registry(std::make_shared<RealClockFactory>());

  // Create a new clock handle.
  zx::clock handle1;
  ASSERT_EQ(ZX_OK, zx::clock::create(
                       ZX_CLOCK_OPT_AUTO_START | ZX_CLOCK_OPT_MONOTONIC | ZX_CLOCK_OPT_CONTINUOUS,
                       nullptr, &handle1));

  zx::clock handle2;
  ASSERT_EQ(ZX_OK, handle1.duplicate(ZX_RIGHT_SAME_RIGHTS, &handle2));

  zx_info_handle_basic_t info;
  ASSERT_EQ(ZX_OK, handle1.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));

  // Create a clock from that handle.
  const uint32_t kDomain = 99;
  std::shared_ptr<Clock> clock1;
  {
    auto result = registry.CreateUserControlledClock(std::move(handle1), "clock", kDomain);
    ASSERT_TRUE(result.is_ok()) << result.status_string();
    clock1 = std::move(result.value());
  }
  ASSERT_NE(clock1.get(), nullptr);
  EXPECT_EQ(clock1->name(), "clock");
  EXPECT_EQ(clock1->domain(), kDomain);
  EXPECT_EQ(clock1->koid(), info.koid);

  // Next call to Find must return the same clock.
  std::shared_ptr<Clock> clock2;
  {
    auto result = registry.FindClock(std::move(handle2));
    ASSERT_TRUE(result.is_ok()) << result.status_string();
    clock2 = std::move(result.value());
  }
  EXPECT_EQ(clock2.get(), clock1.get());
}

TEST(ClockRegistryTest, AddClock) {
  auto realm = SyntheticClockRealm::Create();
  ClockRegistry registry(std::make_shared<SyntheticClockFactory>(realm));

  // Create a new clock.
  const auto kDomain = 42;
  const bool kAdjustable = true;
  auto clock1 = realm->CreateClock("clock", kDomain, kAdjustable);
  auto handle1 = clock1->DuplicateZxClockUnreadable();

  auto add_result = registry.AddClock(clock1);
  ASSERT_TRUE(add_result.is_ok()) << add_result.status_string();

  // Next call to Find must return the same clock.
  auto result = registry.FindClock(handle1);
  ASSERT_TRUE(result.is_ok()) << result.status_string();
  EXPECT_EQ(result.value().get(), clock1.get());
}

TEST(ClockRegistryTest, AddClockFailsAlreadyExists) {
  auto realm = SyntheticClockRealm::Create();
  ClockRegistry registry(std::make_shared<SyntheticClockFactory>(realm));

  // Create a new clock.
  const auto kDomain = 42;
  const bool kAdjustable = true;
  auto clock = realm->CreateClock("clock", kDomain, kAdjustable);

  // First AddClock succeeds.
  {
    auto add_result = registry.AddClock(clock);
    ASSERT_TRUE(add_result.is_ok()) << add_result.status_string();
  }

  // Second AddClock fails.
  {
    auto add_result = registry.AddClock(clock);
    ASSERT_FALSE(add_result.is_ok());
    EXPECT_EQ(add_result.status_value(), ZX_ERR_ALREADY_EXISTS);
  }
}

TEST(ClockRegistryTest, FindClockFailsOnInvalidHandle) {
  auto realm = SyntheticClockRealm::Create();
  ClockRegistry registry(std::make_shared<SyntheticClockFactory>(realm));

  auto result = registry.FindClock(zx::clock());
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.status_value(), ZX_ERR_BAD_HANDLE);
}

TEST(ClockRegistryTest, FindClockFailsOnUnknownClock) {
  auto realm = SyntheticClockRealm::Create();
  ClockRegistry registry(std::make_shared<SyntheticClockFactory>(realm));

  zx::clock handle;
  ASSERT_EQ(ZX_OK, zx::clock::create(
                       ZX_CLOCK_OPT_AUTO_START | ZX_CLOCK_OPT_MONOTONIC | ZX_CLOCK_OPT_CONTINUOUS,
                       nullptr, &handle));

  auto result = registry.FindClock(handle);
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.status_value(), ZX_ERR_NOT_FOUND);
}

TEST(ClockRegistryTest, ForgetClockFailsOnInvalidHandle) {
  auto realm = SyntheticClockRealm::Create();
  ClockRegistry registry(std::make_shared<SyntheticClockFactory>(realm));

  auto result = registry.ForgetClock(zx::clock());
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.status_value(), ZX_ERR_BAD_HANDLE);
}

TEST(ClockRegistryTest, ForgetClockFailsOnUnknownClock) {
  auto realm = SyntheticClockRealm::Create();
  ClockRegistry registry(std::make_shared<SyntheticClockFactory>(realm));

  zx::clock handle;
  ASSERT_EQ(ZX_OK, zx::clock::create(
                       ZX_CLOCK_OPT_AUTO_START | ZX_CLOCK_OPT_MONOTONIC | ZX_CLOCK_OPT_CONTINUOUS,
                       nullptr, &handle));

  auto result = registry.ForgetClock(handle);
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.status_value(), ZX_ERR_NOT_FOUND);
}

}  // namespace media_audio
