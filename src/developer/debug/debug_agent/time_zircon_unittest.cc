// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/clock.h>

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/time.h"

namespace debug_agent {

// Validates that TickTimePoint matches zx::time.
TEST(Time, ZxTimeMatches) {
  auto std_now = std::chrono::steady_clock::now();
  auto zx_now = zx::clock::get_monotonic();

  // We got the zx clock second, so it should always be at least as big.
  EXPECT_LE(std_now.time_since_epoch().count(), zx_now.get());

  // The difference should not be very large. Otherwise the epochs likely differ.
  EXPECT_LE(zx_now.get(), (std_now + std::chrono::seconds(1)).time_since_epoch().count());
}

}  // namespace debug_agent
