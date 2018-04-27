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
    BEGIN_TEST;

    zx_handle_t virt_interrupt_handle;
    zx_handle_t virt_interrupt_handle_cancelled;
    zx_handle_t rsrc = get_root_resource();
    zx_time_t signaled_timestamp = 12345;
    zx_time_t timestamp;

    ASSERT_EQ(zx_irq_create(rsrc, 0, ZX_INTERRUPT_VIRTUAL, &virt_interrupt_handle), ZX_OK, "");
    ASSERT_EQ(zx_irq_create(rsrc, 0, ZX_INTERRUPT_VIRTUAL, &virt_interrupt_handle_cancelled), ZX_OK, "");
    ASSERT_EQ(zx_irq_create(rsrc, 0, ZX_INTERRUPT_SLOT_USER, &virt_interrupt_handle), ZX_ERR_INVALID_ARGS, "");

    ASSERT_EQ(zx_irq_destroy(virt_interrupt_handle_cancelled), ZX_OK, "");
    ASSERT_EQ(zx_irq_trigger(virt_interrupt_handle_cancelled, 0, signaled_timestamp), ZX_ERR_CANCELED, "");

    ASSERT_EQ(zx_irq_trigger(virt_interrupt_handle, 0, signaled_timestamp), ZX_OK, "");

    ASSERT_EQ(zx_irq_wait(virt_interrupt_handle_cancelled, &timestamp), ZX_ERR_CANCELED, "");
    ASSERT_EQ(zx_irq_wait(virt_interrupt_handle, &timestamp), ZX_OK, "");
    ASSERT_EQ(timestamp, signaled_timestamp, "");

    ASSERT_EQ(zx_irq_trigger(virt_interrupt_handle, 0, signaled_timestamp), ZX_OK, "");
    ASSERT_EQ(zx_irq_wait(virt_interrupt_handle, NULL), ZX_OK, "");

    ASSERT_EQ(zx_handle_close(virt_interrupt_handle), ZX_OK, "");
    ASSERT_EQ(zx_handle_close(virt_interrupt_handle_cancelled), ZX_OK, "");
    ASSERT_EQ(zx_irq_trigger(virt_interrupt_handle, 0, signaled_timestamp), ZX_ERR_BAD_HANDLE, "");

    END_TEST;
}
BEGIN_TEST_CASE(interrupt_tests)
RUN_TEST(interrupt_test)
END_TEST_CASE(interrupt_tests)