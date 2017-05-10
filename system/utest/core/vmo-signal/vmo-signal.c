// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdbool.h>

#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>

#include <unittest/unittest.h>

// Test that VMO handles support user signals
bool vmo_signal_test(void) {
    BEGIN_TEST;

    mx_handle_t vmo;
    ASSERT_EQ(mx_vmo_create(4096, 0, &vmo), NO_ERROR, "");
    ASSERT_GT(vmo, 0, "mx_vmo_create() failed");

    mx_signals_t out_signals = 0;
    ASSERT_EQ(mx_object_wait_one(vmo, MX_USER_SIGNAL_0, mx_deadline_after(1), &out_signals),
              ERR_TIMED_OUT, "");
    ASSERT_EQ(out_signals, MX_SIGNAL_LAST_HANDLE, "out_signals not zero after wait timed out");
    ASSERT_EQ(mx_object_signal(vmo, 0, MX_USER_SIGNAL_0), NO_ERROR, "");
    ASSERT_EQ(
        mx_object_wait_one(vmo, MX_USER_SIGNAL_0, MX_TIME_INFINITE, &out_signals), NO_ERROR, "");
    ASSERT_EQ(
        out_signals, MX_USER_SIGNAL_0 | MX_SIGNAL_LAST_HANDLE,
        "MX_USER_SIGNAL_0 not set after successful wait");

    EXPECT_EQ(mx_handle_close(vmo), NO_ERROR, "");

    END_TEST;
}

BEGIN_TEST_CASE(vmo_signal_tests)
RUN_TEST(vmo_signal_test)
END_TEST_CASE(vmo_signal_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
