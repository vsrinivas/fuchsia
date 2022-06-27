// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/real_clock_factory.h"

#include <gtest/gtest.h>

namespace media_audio {

TEST(RealClockFactoryTest, CreateGraphControlledClock) {
  RealClockFactory factory;

  auto result = factory.CreateGraphControlledClock("clock name");
  ASSERT_TRUE(result.is_ok()) << result.status_string();

  auto& clock = result.value().first;
  auto& handle = result.value().second;

  // Check properties of the returned clock.
  zx_info_handle_basic_t info;
  auto status = handle.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  ASSERT_EQ(status, ZX_OK);

  ASSERT_NE(clock.get(), nullptr);
  EXPECT_EQ(clock->name(), "clock name");
  EXPECT_EQ(clock->domain(), Clock::kExternalDomain);
  EXPECT_EQ(clock->koid(), info.koid);
  EXPECT_TRUE(clock->adjustable());

  // The handle must be readable but not writable.
  EXPECT_EQ(info.rights, ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_READ);

  zx_time_t unused;
  status = handle.read(&unused);
  EXPECT_EQ(status, ZX_OK);
}

TEST(RealClockFactoryTest, CreateWrappedClock) {
  RealClockFactory factory;

  zx::clock handle;
  auto status = zx::clock::create(
      ZX_CLOCK_OPT_AUTO_START | ZX_CLOCK_OPT_MONOTONIC | ZX_CLOCK_OPT_CONTINUOUS, nullptr, &handle);
  ASSERT_EQ(status, ZX_OK);

  zx_info_handle_basic_t info;
  status = handle.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  ASSERT_EQ(status, ZX_OK);

  const uint32_t kDomain = 42;
  const bool kAdjustable = false;
  auto result = factory.CreateWrappedClock(std::move(handle), "clock name", kDomain, kAdjustable);
  ASSERT_TRUE(result.is_ok()) << result.status_string();

  auto& clock = result.value();
  ASSERT_NE(clock.get(), nullptr);
  EXPECT_EQ(clock->name(), "clock name");
  EXPECT_EQ(clock->domain(), kDomain);
  EXPECT_EQ(clock->koid(), info.koid);
  EXPECT_EQ(clock->adjustable(), kAdjustable);
}

}  // namespace media_audio
