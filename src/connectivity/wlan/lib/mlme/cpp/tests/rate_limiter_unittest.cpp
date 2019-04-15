// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <wlan/mlme/rate_limiter.h>

namespace wlan {

TEST(RateLimiter, SingleEvent) {
  RateLimiter limiter(zx::duration(ZX_MSEC(100)), 1);
  EXPECT_TRUE(limiter.RecordEvent(zx::time(ZX_MSEC(2000))));
  EXPECT_FALSE(limiter.RecordEvent(zx::time(ZX_MSEC(2099))));
  EXPECT_TRUE(limiter.RecordEvent(zx::time(ZX_MSEC(2100))));
  EXPECT_FALSE(limiter.RecordEvent(zx::time(ZX_MSEC(2101))));
  EXPECT_FALSE(limiter.RecordEvent(zx::time(ZX_MSEC(2199))));
  EXPECT_TRUE(limiter.RecordEvent(zx::time(ZX_MSEC(2200))));
}

TEST(RateLimiter, TwoEvents) {
  RateLimiter limiter(zx::duration(ZX_MSEC(100)), 2);
  EXPECT_TRUE(limiter.RecordEvent(zx::time(ZX_MSEC(2000))));
  EXPECT_TRUE(limiter.RecordEvent(zx::time(ZX_MSEC(2050))));
  EXPECT_FALSE(limiter.RecordEvent(zx::time(ZX_MSEC(2099))));
  EXPECT_TRUE(limiter.RecordEvent(zx::time(ZX_MSEC(2100))));
  EXPECT_FALSE(limiter.RecordEvent(zx::time(ZX_MSEC(2149))));
  EXPECT_TRUE(limiter.RecordEvent(zx::time(ZX_MSEC(2150))));
}

}  // namespace wlan
