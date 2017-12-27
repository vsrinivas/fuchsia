// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>
#include <zircon/syscalls.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <unistd.h>

extern zx_handle_t get_root_resource(void);

// Tests support for virtual interrupts
static bool interrupt_test(void) {
    const uint32_t BOUND_SLOT = 0;
    const uint32_t UNBOUND_SLOT = 1;

    BEGIN_TEST;

    zx_handle_t handle;
    zx_handle_t rsrc = get_root_resource();
    uint64_t slots;
    zx_time_t timestamp;
    zx_time_t signaled_timestamp = 12345;

    ASSERT_EQ(zx_interrupt_create(rsrc, 0, &handle), ZX_OK, "");
    ASSERT_EQ(zx_interrupt_bind(handle, ZX_INTERRUPT_SLOT_USER, rsrc, 0, ZX_INTERRUPT_VIRTUAL),
              ZX_ERR_ALREADY_BOUND, "");
    ASSERT_EQ(zx_interrupt_bind(handle, ZX_INTERRUPT_MAX_SLOTS + 1, rsrc, 0, ZX_INTERRUPT_VIRTUAL),
              ZX_ERR_INVALID_ARGS, "");
    ASSERT_EQ(zx_interrupt_bind(handle, BOUND_SLOT, rsrc, 0, ZX_INTERRUPT_VIRTUAL), ZX_OK, "");
    ASSERT_EQ(zx_interrupt_bind(handle, BOUND_SLOT, rsrc, 0, ZX_INTERRUPT_VIRTUAL),
                                ZX_ERR_ALREADY_BOUND, "");

    ASSERT_EQ(zx_interrupt_get_timestamp(handle, BOUND_SLOT, &timestamp), ZX_ERR_BAD_STATE, "");

    ASSERT_EQ(zx_interrupt_signal(handle, UNBOUND_SLOT, signaled_timestamp),
                                  ZX_ERR_NOT_FOUND, "");
    ASSERT_EQ(zx_interrupt_signal(handle, BOUND_SLOT, signaled_timestamp), ZX_OK, "");

    ASSERT_EQ(zx_interrupt_wait(handle, &slots), ZX_OK, "");
    ASSERT_EQ(slots, (1ul << BOUND_SLOT), "");

    ASSERT_EQ(zx_interrupt_get_timestamp(handle, UNBOUND_SLOT, &timestamp), ZX_ERR_NOT_FOUND, "");
    ASSERT_EQ(zx_interrupt_get_timestamp(handle, BOUND_SLOT, &timestamp), ZX_OK, "");
    ASSERT_EQ(timestamp, signaled_timestamp, "");

    ASSERT_EQ(zx_handle_close(handle), ZX_OK, "");

    END_TEST;
}

// Tests support for multiple virtual interrupts
static bool interrupt_test_multiple(void) {
    BEGIN_TEST;

    zx_handle_t handle;
    zx_handle_t rsrc = get_root_resource();
    uint64_t slots;
    zx_time_t timestamp;
    zx_time_t signaled_timestamp = 1;

    ASSERT_EQ(zx_interrupt_create(rsrc, 0, &handle), ZX_OK, "");

    for (uint32_t slot = 0; slot < ZX_INTERRUPT_SLOT_USER; slot++) {
        ASSERT_EQ(zx_interrupt_bind(handle, slot, rsrc, 0, ZX_INTERRUPT_VIRTUAL), ZX_OK, "");
    }

    for (uint32_t slot = 0; slot < ZX_INTERRUPT_SLOT_USER; slot++, signaled_timestamp++) {
        ASSERT_EQ(zx_interrupt_signal(handle, slot, signaled_timestamp), ZX_OK, "");
        ASSERT_EQ(zx_interrupt_wait(handle, &slots), ZX_OK, "");
        ASSERT_EQ(slots, (1ul << slot), "");
        ASSERT_EQ(zx_interrupt_get_timestamp(handle, slot, &timestamp), ZX_OK, "");
        ASSERT_EQ(timestamp, signaled_timestamp, "");
    }

    ASSERT_EQ(zx_handle_close(handle), ZX_OK, "");

    END_TEST;
}

BEGIN_TEST_CASE(interrupt_tests)
RUN_TEST(interrupt_test)
RUN_TEST(interrupt_test_multiple)
END_TEST_CASE(interrupt_tests)
