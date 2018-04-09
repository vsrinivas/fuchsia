// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#include <lib/fzl/time.h>
#include <fbl/algorithm.h>
#include <unittest/unittest.h>
#include <zircon/syscalls.h>

namespace {

template <typename T>
bool AlmostEqual(T t0, T t1, T e) {
    BEGIN_HELPER;

    char buf[128];
    snprintf(buf, sizeof(buf), "%zu != %zu (within error of %zu)", t0, t1, e);
    ASSERT_TRUE(fbl::min(t0, t1) + e >= fbl::max(t0, t1), buf);

    END_HELPER;
}

bool TickConverter(zx::ticks ticks, zx::ticks err) {
    BEGIN_HELPER;
    ASSERT_TRUE(AlmostEqual(ticks.get(), fzl::NsToTicks(fzl::TicksToNs(ticks)).get(), err.get()));
    ASSERT_TRUE(AlmostEqual(ticks.get(), ns_to_ticks(ticks_to_ns(ticks.get())), err.get()));
    END_HELPER;
}

bool NsConverter(zx::duration ns, zx::duration err) {
    BEGIN_HELPER;
    ASSERT_TRUE(AlmostEqual(ns.get(), fzl::TicksToNs(fzl::NsToTicks(ns)).get(), err.get()));
    ASSERT_TRUE(AlmostEqual(ns.get(), ticks_to_ns(ns_to_ticks(ns.get())), err.get()));
    END_HELPER;
}

bool TimeTest() {
    BEGIN_TEST;

    zx::ticks tps = zx::ticks::per_second();
    zx::duration nps = zx::sec(1);

    // The following tests check converting from:
    //  - ticks --> nanoseconds --> ticks
    //  - nanoseconds --> ticks --> nanoseconds
    //
    // This conversion is inherently lossy if the number of ticks/ns (or
    // ns/tick) is not an exact integer -- which is almost always the case.
    //
    // To convert N nanoseconds to ticks, we'd logically multiply by
    // "ticks/sec" / "ns/second". However, by converting N into the ticks
    // equivalent T, we may be losing the fractional component of this number: N
    // may actually be represented by T +/- a partial tick.
    //
    // In most situations, where ticks are higher precision than nanoseconds,
    // there will actually be even more loss in the other direction: when
    // converting from ticks to nanoseconds, we may potentially lose as many as
    // "ticks/second / ns/second" ticks.
    //
    // To ensure our error margins account for this loss, where we lose
    // minimally a "partial unit" and maximally an integer ratio of the units,
    // we calculate acceptable loss as:
    //
    // loss = max(1 + ratio, 1)
    //
    // Where we add one to the ratio to "round up to the nearest integer ratio" while
    // doing the conversion.
    zx::ticks tick_loss = fbl::max(zx::ticks(1 + (tps.get() / nps.get())),
                                   zx::ticks(1));
    zx::duration duration_loss = fbl::max(zx::duration(1 + (nps.get() / tps.get())),
                                          zx::duration(1));

    ASSERT_TRUE(TickConverter(zx::ticks(0), zx::ticks(0)));
    ASSERT_TRUE(TickConverter(zx::ticks(50), tick_loss));
    ASSERT_TRUE(TickConverter(zx::ticks(100), tick_loss));
    ASSERT_TRUE(TickConverter(zx::ticks(100000), tick_loss));
    ASSERT_TRUE(TickConverter(zx::ticks(1000000000), tick_loss));
    ASSERT_TRUE(TickConverter(zx::ticks(10000000000000), tick_loss));

    ASSERT_TRUE(NsConverter(zx::duration(0), zx::duration(0)));
    ASSERT_TRUE(NsConverter(zx::duration(50), duration_loss));
    ASSERT_TRUE(NsConverter(zx::duration(100), duration_loss));
    ASSERT_TRUE(NsConverter(zx::duration(100000), duration_loss));
    ASSERT_TRUE(NsConverter(zx::duration(1000000000), duration_loss));
    ASSERT_TRUE(NsConverter(zx::duration(10000000000000), duration_loss));

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(libfzl_tests)
RUN_TEST(TimeTest)
END_TEST_CASE(libfzl_tests)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
