// Copyright 2016 The Fuchsia Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>

#include <lib/zx/time.h>
#include <zircon/syscalls.h>
#include <zxtest/zxtest.h>

namespace {

// Calculation of elapsed time using ticks.
TEST(TicksTest, ElapsedTimeUsingTicks) {
    zx::ticks ticks_per_second = zx::ticks::per_second();
    ASSERT_GT(ticks_per_second, zx::ticks(0), "Invalid ticks per second");

    zx::ticks start = zx::ticks::now();
    zx::ticks end = zx::ticks::now();
    ASSERT_GE(end, start, "Ticks went backwards");

    double seconds =  static_cast<double>((end - start).get()) /
                          static_cast<double>(ticks_per_second.get());
    ASSERT_GE(seconds, 0, "Time went backwards");
}

} // namespace
