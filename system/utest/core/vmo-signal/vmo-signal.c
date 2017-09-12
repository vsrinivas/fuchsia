// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdbool.h>

#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>

#include <unittest/unittest.h>

// Test that VMO handles support user signals
bool vmo_signal_test(void) {
    BEGIN_TEST;

    zx_handle_t vmo = ZX_HANDLE_INVALID;
    ASSERT_EQ(zx_vmo_create(4096, 0, &vmo), ZX_OK, "");
    ASSERT_NE(vmo, ZX_HANDLE_INVALID, "zx_vmo_create() failed");

    zx_signals_t out_signals = 0;
    ASSERT_EQ(zx_object_wait_one(vmo, ZX_USER_SIGNAL_0, zx_deadline_after(1), &out_signals),
              ZX_ERR_TIMED_OUT, "");
    ASSERT_EQ(out_signals, ZX_SIGNAL_LAST_HANDLE, "out_signals not zero after wait timed out");
    ASSERT_EQ(zx_object_signal(vmo, 0, ZX_USER_SIGNAL_0), ZX_OK, "");
    ASSERT_EQ(
        zx_object_wait_one(vmo, ZX_USER_SIGNAL_0, ZX_TIME_INFINITE, &out_signals), ZX_OK, "");
    ASSERT_EQ(
        out_signals, ZX_USER_SIGNAL_0 | ZX_SIGNAL_LAST_HANDLE,
        "ZX_USER_SIGNAL_0 not set after successful wait");

    EXPECT_EQ(zx_handle_close(vmo), ZX_OK, "");

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
