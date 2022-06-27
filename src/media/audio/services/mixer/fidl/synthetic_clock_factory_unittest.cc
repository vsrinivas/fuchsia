// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/synthetic_clock_factory.h"

#include <gtest/gtest.h>

namespace media_audio {

TEST(SyntheticClockFactoryTest, CreateGraphControlledClock) {
  auto realm(SyntheticClockRealm::Create());
  SyntheticClockFactory factory(realm);

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

  // The handle must not be readable or writable.
  EXPECT_EQ(info.rights, ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER);

  zx_time_t unused;
  status = handle.read(&unused);
  EXPECT_NE(status, ZX_OK);
}

TEST(SyntheticClockFactoryTest, CreateWrappedClock) {
  auto realm(SyntheticClockRealm::Create());
  SyntheticClockFactory factory(realm);

  auto result = factory.CreateWrappedClock(zx::clock(), "clock", 42, false);
  ASSERT_FALSE(result.is_ok()) << result.status_string();
  EXPECT_EQ(result.status_value(), ZX_ERR_NOT_SUPPORTED);
}

}  // namespace media_audio
