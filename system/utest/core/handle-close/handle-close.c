// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>

#define kNumEventpairs 4u
#define kGap 2u

static bool handle_close_many_test(void) {
    BEGIN_TEST;

    // Layout: 0 1 2 3 : 0 1 2 3
    zx_handle_t eventpairs[kNumEventpairs * 2];

    for (size_t idx = 0u; idx < kNumEventpairs; ++idx) {
        ASSERT_EQ(zx_eventpair_create(0u, &eventpairs[idx], &eventpairs[idx + kNumEventpairs]), ZX_OK, "");
    }

    ASSERT_EQ(zx_handle_close_many(&eventpairs[0u], kNumEventpairs), ZX_OK, "");

    // Assert that every handle in the preceeding close call was in
    // fact closed, by waiting on the PEER_CLOSED signal.
    for (size_t idx = kNumEventpairs; idx < 8u; ++idx) {
        zx_signals_t signals;
        ASSERT_EQ(zx_object_wait_one(eventpairs[idx], ZX_EVENTPAIR_PEER_CLOSED,
                                     0u, &signals), ZX_OK, "");
        ASSERT_EQ(signals & ZX_EVENTPAIR_PEER_CLOSED, ZX_EVENTPAIR_PEER_CLOSED, "");
    }

    ASSERT_EQ(zx_handle_close_many(&eventpairs[kNumEventpairs], kNumEventpairs), ZX_OK, "");

    END_TEST;
}

static bool handle_close_many_invalid_test(void) {
    BEGIN_TEST;

    // Layout: 0 1 2 3 : invalid invalid : 0 1 2 3
    zx_handle_t eventpairs[kNumEventpairs * 2 + kGap];

    for (size_t idx = 0u; idx < kNumEventpairs; ++idx) {
        ASSERT_EQ(zx_eventpair_create(0u, &eventpairs[idx],
                                      &eventpairs[idx + kNumEventpairs + kGap]), ZX_OK, "");
    }
    eventpairs[kNumEventpairs] = ZX_HANDLE_INVALID;
    eventpairs[kNumEventpairs + 1] = ZX_HANDLE_INVALID;

    ASSERT_EQ(zx_handle_close_many(&eventpairs[0u], kNumEventpairs + kGap), ZX_OK, "");

    // Assert that every handle in the preceeding close call was in
    // fact closed, by waiting on the PEER_CLOSED signal.
    for (size_t idx = kNumEventpairs + kGap; idx < 10u; ++idx) {
        zx_signals_t signals;
        ASSERT_EQ(zx_object_wait_one(eventpairs[idx], ZX_EVENTPAIR_PEER_CLOSED,
                                     0u, &signals), ZX_OK, "");
        ASSERT_EQ(signals & ZX_EVENTPAIR_PEER_CLOSED, ZX_EVENTPAIR_PEER_CLOSED, "");
    }

    ASSERT_EQ(zx_handle_close_many(&eventpairs[kNumEventpairs + kGap], kNumEventpairs), ZX_OK, "");

    END_TEST;
}

static bool handle_close_many_duplicate_test(void) {
    BEGIN_TEST;

    // Layout: 0 1 0 1 2 3 : 0 1 2 3
    zx_handle_t eventpairs[kNumEventpairs * 2 + kGap];

    for (size_t idx = kGap; idx < kGap + kNumEventpairs; ++idx) {
        ASSERT_EQ(zx_eventpair_create(0u, &eventpairs[idx],
                                      &eventpairs[idx + kNumEventpairs]), ZX_OK, "");
    }
    // Duplicate the values at the start.
    eventpairs[0u] = eventpairs[kGap];
    eventpairs[1u] = eventpairs[kGap + 1u];

    // Note that this returns an error value: the duplicated handles
    // can't be closed twice. Despite this, all handles were closed.
    ASSERT_EQ(zx_handle_close_many(&eventpairs[0u], kNumEventpairs + kGap), ZX_ERR_BAD_HANDLE, "");

    // Assert that every handle in the preceeding close call was in
    // fact closed, by waiting on the PEER_CLOSED signal.
    for (size_t idx = kNumEventpairs + kGap; idx < 10u; ++idx) {
        zx_signals_t signals;
        ASSERT_EQ(zx_object_wait_one(eventpairs[idx], ZX_EVENTPAIR_PEER_CLOSED,
                                     0u, &signals), ZX_OK, "");
        ASSERT_EQ(signals & ZX_EVENTPAIR_PEER_CLOSED, ZX_EVENTPAIR_PEER_CLOSED, "");
    }

    ASSERT_EQ(zx_handle_close_many(&eventpairs[kNumEventpairs + kGap], kNumEventpairs), ZX_OK, "");

    END_TEST;
}

BEGIN_TEST_CASE(handle_close_tests)
RUN_TEST(handle_close_many_test)
RUN_TEST(handle_close_many_invalid_test)
RUN_TEST(handle_close_many_duplicate_test)
END_TEST_CASE(handle_close_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
