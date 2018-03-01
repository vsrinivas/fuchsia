// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdbool.h>

#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <zircon/process.h>

#include <unittest/unittest.h>

// Test that VMO handles support user signals
static bool vmo_signal_sanity_test(void) {
    BEGIN_TEST;

    zx_handle_t vmo = ZX_HANDLE_INVALID;
    ASSERT_EQ(zx_vmo_create(4096, 0, &vmo), ZX_OK, "");
    ASSERT_NE(vmo, ZX_HANDLE_INVALID, "zx_vmo_create() failed");

    zx_signals_t out_signals = 0;

    // This is not timing dependent, if this fails is not a flake.
    ASSERT_EQ(zx_object_wait_one(
        vmo, ZX_USER_SIGNAL_0, zx_deadline_after(2), &out_signals), ZX_ERR_TIMED_OUT, "");

    ASSERT_EQ(out_signals, ZX_VMO_ZERO_CHILDREN, "unexpected initial signal set");
    ASSERT_EQ(zx_object_signal(vmo, 0, ZX_USER_SIGNAL_0), ZX_OK, "");
    ASSERT_EQ(zx_object_wait_one(
        vmo, ZX_USER_SIGNAL_0, ZX_TIME_INFINITE, &out_signals), ZX_OK, "");
    ASSERT_EQ(
        out_signals, ZX_USER_SIGNAL_0 | ZX_VMO_ZERO_CHILDREN,
        "ZX_USER_SIGNAL_0 not set after successful wait");

    ASSERT_EQ(zx_handle_close(vmo), ZX_OK, "");

    END_TEST;
}

static zx_status_t vmo_has_no_children(zx_handle_t vmo) {
    zx_signals_t signals;
    return zx_object_wait_one(vmo, ZX_VMO_ZERO_CHILDREN, ZX_TIME_INFINITE, &signals);
}

static zx_status_t vmo_has_children(zx_handle_t vmo) {
    zx_signals_t signals;
    zx_status_t res = zx_object_wait_one(
        vmo, ZX_VMO_ZERO_CHILDREN, zx_deadline_after(2), &signals);
    return (res == ZX_ERR_TIMED_OUT) ? ZX_OK : res;
}

static bool vmo_child_signal_clone_test(void) {
    BEGIN_TEST;

    zx_handle_t vmo = ZX_HANDLE_INVALID;
    ASSERT_EQ(zx_vmo_create(4096u * 2, 0, &vmo), ZX_OK, "");
    ASSERT_NE(vmo, ZX_HANDLE_INVALID, "");

    zx_handle_t clone = ZX_HANDLE_INVALID;
    zx_handle_t clone2 = ZX_HANDLE_INVALID;

    // The waits below with timeout are not timing dependent, if this fails is not a flake.

    for (int ix = 0; ix != 10; ++ix) {
        ASSERT_EQ(vmo_has_no_children(vmo), ZX_OK, "");

        ASSERT_EQ(zx_vmo_clone(
            vmo, ZX_VMO_CLONE_COPY_ON_WRITE, 0u, 4096u, &clone), ZX_OK, "");

        ASSERT_EQ(vmo_has_no_children(clone), ZX_OK, "");
        ASSERT_EQ(vmo_has_children(vmo), ZX_OK, "");

        ASSERT_EQ(zx_vmo_clone(
            clone, ZX_VMO_CLONE_COPY_ON_WRITE, 0u, 4096u, &clone2), ZX_OK, "");

        ASSERT_EQ(vmo_has_no_children(clone2), ZX_OK, "");
        ASSERT_EQ(vmo_has_children(clone), ZX_OK, "");
        ASSERT_EQ(vmo_has_children(vmo), ZX_OK, "");

        ASSERT_EQ(zx_handle_close(clone), ZX_OK, "");
        ASSERT_EQ(vmo_has_children(vmo), ZX_OK, "");
        ASSERT_EQ(vmo_has_no_children(clone2), ZX_OK, "");

        ASSERT_EQ(zx_handle_close(clone2), ZX_OK, "");
    }

    ASSERT_EQ(zx_handle_close(vmo), ZX_OK, "");

    END_TEST;
}

static bool vmo_child_signal_map_test(void) {
    BEGIN_TEST;

    zx_handle_t vmo = ZX_HANDLE_INVALID;
    ASSERT_EQ(zx_vmo_create(4096u * 2, 0, &vmo), ZX_OK, "");
    ASSERT_NE(vmo, ZX_HANDLE_INVALID, "");

    zx_handle_t clone = ZX_HANDLE_INVALID;

    uint32_t flags = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE;

    for (int ix = 0; ix != 10; ++ix) {
        ASSERT_EQ(vmo_has_no_children(vmo), ZX_OK, "");

        ASSERT_EQ(zx_vmo_clone(
            vmo, ZX_VMO_CLONE_COPY_ON_WRITE, 0u, 4096u, &clone), ZX_OK, "");

        uintptr_t addr = 0;
        ASSERT_EQ(zx_vmar_map(
            zx_vmar_root_self(), 0u, clone, 0, 4096u, flags, &addr), ZX_OK, "");

        ASSERT_EQ(vmo_has_children(vmo), ZX_OK, "");

        ASSERT_EQ(zx_handle_close(clone), ZX_OK, "");

        ASSERT_EQ(vmo_has_children(vmo), ZX_OK, "");

        ASSERT_EQ(zx_vmar_unmap(zx_vmar_root_self(), addr, 4096u), ZX_OK, "");
    }

    ASSERT_EQ(zx_handle_close(vmo), ZX_OK, "");

    END_TEST;
}

BEGIN_TEST_CASE(vmo_signal_tests)
RUN_TEST(vmo_signal_sanity_test)
RUN_TEST(vmo_child_signal_clone_test)
RUN_TEST(vmo_child_signal_map_test)
END_TEST_CASE(vmo_signal_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
