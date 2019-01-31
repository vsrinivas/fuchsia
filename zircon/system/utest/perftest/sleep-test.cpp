// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/string_printf.h>
#include <perftest/perftest.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>

namespace {

// Test sleeping for different lengths of time.
//
// This serves an example of a parameterized perf test.
//
// This can be useful for measuring the overhead of sleeping.  It can also
// be used to measure the variation in actual sleep times.  Checking for
// under-sleeps and over-sleeps can serve as a sanity check for the
// perftest framework.
//
// Ideally we would be able to test a continuous range of sleep times,
// which might reveal discontinuities in the actual sleep times.  The
// perftest framework does not support this yet.
bool SleepTest(perftest::RepeatState* state, zx_duration_t delay_ns) {
    while (state->KeepRunning()) {
        ZX_ASSERT(zx_nanosleep(zx_deadline_after(delay_ns)) == ZX_OK);
    }
    return true;
}

void RegisterTests() {
    static const zx_duration_t kTimesNs[] = {
        0,
        1,
        10,
        100,
        1000,
        10000,
    };
    for (auto time_ns : kTimesNs) {
        auto name = fbl::StringPrintf(
            "Sleep/%lluns", static_cast<unsigned long long>(time_ns));
        perftest::RegisterTest(name.c_str(), SleepTest, time_ns);
    }
}
PERFTEST_CTOR(RegisterTests);

}  // namespace
