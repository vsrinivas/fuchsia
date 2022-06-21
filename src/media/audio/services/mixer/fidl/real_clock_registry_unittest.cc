// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/real_clock_registry.h"

#include <gtest/gtest.h>

namespace media_audio {

TEST(RealClockRegistryTest, CreateGraphControlled) {
  RealClockRegistry registry;
  auto zx_clock = registry.CreateGraphControlled();

  zx_info_handle_basic_t info;
  auto status = zx_clock.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  ASSERT_EQ(status, ZX_OK);

  // Must be readable but not writable.
  EXPECT_EQ(info.rights, ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_READ);

  zx_time_t unused;
  status = zx_clock.read(&unused);
  EXPECT_EQ(status, ZX_OK);

  // Must find this clock.
  auto clock = registry.FindOrCreate(std::move(zx_clock), "unused", 42);
  ASSERT_NE(clock.get(), nullptr);
  EXPECT_EQ(clock->name(), "GraphControlled0");
  EXPECT_EQ(clock->domain(), Clock::kExternalDomain);
  EXPECT_EQ(clock->koid(), info.koid);
}

TEST(RealClockRegistryTest, CreateThenFind) {
  RealClockRegistry registry;

  zx::clock zx_clock1;
  auto status =
      zx::clock::create(ZX_CLOCK_OPT_AUTO_START | ZX_CLOCK_OPT_MONOTONIC | ZX_CLOCK_OPT_CONTINUOUS,
                        nullptr, &zx_clock1);
  ASSERT_EQ(status, ZX_OK);

  zx::clock zx_clock2;
  status = zx_clock1.duplicate(ZX_RIGHT_SAME_RIGHTS, &zx_clock2);
  ASSERT_EQ(status, ZX_OK);

  zx_info_handle_basic_t info;
  status = zx_clock1.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  ASSERT_EQ(status, ZX_OK);

  // Create.
  const uint32_t kDomain = 42;
  auto clock1 = registry.FindOrCreate(std::move(zx_clock1), "clock", kDomain);
  ASSERT_NE(clock1.get(), nullptr);
  EXPECT_EQ(clock1->name(), "clock");
  EXPECT_EQ(clock1->domain(), kDomain);
  EXPECT_EQ(clock1->koid(), info.koid);

  // Must return the same clock.
  auto clock2 = registry.FindOrCreate(std::move(zx_clock2), "unused", kDomain);
  EXPECT_EQ(clock2.get(), clock1.get());
}

TEST(RealClockRegistryTest, FindFailsOnInvalidHandle) {
  RealClockRegistry registry;
  auto clock = registry.FindOrCreate(zx::clock(), "clock", 42);
  ASSERT_EQ(clock.get(), nullptr);
}

}  // namespace media_audio
