// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <unistd.h>

extern zx_handle_t get_root_resource(void);

// Tests to bind interrupt to a non-bindable port
static bool interrupt_port_non_bindable_test(void) {
    BEGIN_TEST;

    zx_handle_t port_handle;
    zx_handle_t virt_interrupt_port_handle;
    zx_handle_t rsrc = get_root_resource();
    uint32_t key = 789;

    ASSERT_EQ(zx_interrupt_create(rsrc, 0, ZX_INTERRUPT_VIRTUAL,
                                  &virt_interrupt_port_handle), ZX_OK, "");
    ASSERT_EQ(zx_port_create(0, &port_handle), ZX_OK, "");

    ASSERT_EQ(zx_interrupt_bind(virt_interrupt_port_handle,
                                port_handle, key, 0), ZX_ERR_WRONG_TYPE, "");

    ASSERT_EQ(zx_handle_close(port_handle), ZX_OK, "");
    ASSERT_EQ(zx_handle_close(virt_interrupt_port_handle), ZX_OK, "");

    END_TEST;
}

// Tests Interrupts bound to a port
static bool interrupt_port_bound_test(void) {
    BEGIN_TEST;

    zx_handle_t virt_interrupt_port_handle;
    zx_handle_t port_handle_bind;
    zx_time_t signaled_timestamp_1 = 12345;
    zx_time_t signaled_timestamp_2 = 67890;
    uint32_t key = 789;
    zx_port_packet_t out;
    zx_handle_t rsrc = get_root_resource();

    ASSERT_EQ(zx_interrupt_create(rsrc, 0, ZX_INTERRUPT_VIRTUAL,
                                  &virt_interrupt_port_handle), ZX_OK, "");
    ASSERT_EQ(zx_port_create(1, &port_handle_bind), ZX_OK, "");

    // Test port binding
    ASSERT_EQ(zx_interrupt_bind(virt_interrupt_port_handle, port_handle_bind, key, 0), ZX_OK, "");
    ASSERT_EQ(zx_interrupt_trigger(virt_interrupt_port_handle, 0, signaled_timestamp_1), ZX_OK, "");
    ASSERT_EQ(zx_port_wait(port_handle_bind, ZX_TIME_INFINITE, &out, 1), ZX_OK, "");
    ASSERT_EQ(out.interrupt.timestamp, signaled_timestamp_1, "");

    // Triggering 2nd time, ACKing it causes port packet to be delivered
    ASSERT_EQ(zx_interrupt_trigger(virt_interrupt_port_handle, 0, signaled_timestamp_1), ZX_OK, "");
    ASSERT_EQ(zx_interrupt_ack(virt_interrupt_port_handle), ZX_OK, "");
    ASSERT_EQ(zx_port_wait(port_handle_bind, ZX_TIME_INFINITE, &out, 1), ZX_OK, "");
    ASSERT_EQ(out.interrupt.timestamp, signaled_timestamp_1, "");
    ASSERT_EQ(out.key, key, "");
    ASSERT_EQ(out.type, ZX_PKT_TYPE_INTERRUPT, "");
    ASSERT_EQ(out.status, ZX_OK, "");
    ASSERT_EQ(zx_interrupt_ack(virt_interrupt_port_handle), ZX_OK, "");

    // Triggering it twice
    // the 2nd timestamp is recorded and upon ACK another packet is queued
    ASSERT_EQ(zx_interrupt_trigger(virt_interrupt_port_handle, 0, signaled_timestamp_1), ZX_OK, "");
    ASSERT_EQ(zx_interrupt_trigger(virt_interrupt_port_handle, 0, signaled_timestamp_2), ZX_OK, "");
    ASSERT_EQ(zx_port_wait(port_handle_bind, ZX_TIME_INFINITE, &out, 1), ZX_OK, "");
    ASSERT_EQ(out.interrupt.timestamp, signaled_timestamp_1, "");
    ASSERT_EQ(zx_interrupt_ack(virt_interrupt_port_handle), ZX_OK, "");
    ASSERT_EQ(zx_port_wait(port_handle_bind, ZX_TIME_INFINITE, &out, 1), ZX_OK, "");
    ASSERT_EQ(out.interrupt.timestamp, signaled_timestamp_2, "");

    // Try to destroy now, expecting to return error telling packet
    // has been read but the interrupt has not been re-armed
    ASSERT_EQ(zx_interrupt_destroy(virt_interrupt_port_handle), ZX_ERR_NOT_FOUND,"");
    ASSERT_EQ(zx_interrupt_ack(virt_interrupt_port_handle), ZX_ERR_CANCELED, "");
    ASSERT_EQ(zx_interrupt_trigger(virt_interrupt_port_handle, 0,
                                   signaled_timestamp_1), ZX_ERR_CANCELED, "");

    ASSERT_EQ(zx_handle_close(virt_interrupt_port_handle), ZX_OK, "");
    ASSERT_EQ(zx_handle_close(port_handle_bind), ZX_OK, "");

    END_TEST;
}

// Tests support for virtual interrupts
static bool interrupt_test(void) {
    BEGIN_TEST;

    zx_handle_t virt_interrupt_handle;
    zx_handle_t virt_interrupt_handle_cancelled;
    zx_time_t timestamp;
    zx_time_t signaled_timestamp = 12345;
    zx_handle_t rsrc = get_root_resource();

    ASSERT_EQ(zx_interrupt_create(rsrc, 0, ZX_INTERRUPT_VIRTUAL,
                                  &virt_interrupt_handle), ZX_OK, "");
    ASSERT_EQ(zx_interrupt_create(rsrc, 0, ZX_INTERRUPT_VIRTUAL,
                                  &virt_interrupt_handle_cancelled), ZX_OK, "");
    ASSERT_EQ(zx_interrupt_create(rsrc, 0, ZX_INTERRUPT_SLOT_USER,
                                  &virt_interrupt_handle), ZX_ERR_INVALID_ARGS, "");


    ASSERT_EQ(zx_interrupt_destroy(virt_interrupt_handle_cancelled), ZX_OK, "");
    ASSERT_EQ(zx_interrupt_trigger(virt_interrupt_handle_cancelled,
                                   0, signaled_timestamp), ZX_ERR_CANCELED, "");

    ASSERT_EQ(zx_interrupt_trigger(virt_interrupt_handle, 0, signaled_timestamp), ZX_OK, "");

    ASSERT_EQ(zx_interrupt_wait(virt_interrupt_handle_cancelled, &timestamp), ZX_ERR_CANCELED, "");
    ASSERT_EQ(zx_interrupt_wait(virt_interrupt_handle, &timestamp), ZX_OK, "");
    ASSERT_EQ(timestamp, signaled_timestamp, "");

    ASSERT_EQ(zx_interrupt_trigger(virt_interrupt_handle, 0, signaled_timestamp), ZX_OK, "");
    ASSERT_EQ(zx_interrupt_wait(virt_interrupt_handle, NULL), ZX_OK, "");

    ASSERT_EQ(zx_handle_close(virt_interrupt_handle), ZX_OK, "");
    ASSERT_EQ(zx_handle_close(virt_interrupt_handle_cancelled), ZX_OK, "");
    ASSERT_EQ(zx_interrupt_trigger(virt_interrupt_handle,
                                   0, signaled_timestamp), ZX_ERR_BAD_HANDLE, "");

    END_TEST;
}

BEGIN_TEST_CASE(interrupt_tests)
RUN_TEST(interrupt_test)
RUN_TEST(interrupt_port_bound_test)
RUN_TEST(interrupt_port_non_bindable_test)
END_TEST_CASE(interrupt_tests)