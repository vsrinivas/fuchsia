// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>
#include <zircon/syscalls.h>
#include <zircon/process.h>
#include <zircon/syscalls/port.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <unistd.h>
#include <threads.h>

extern zx_handle_t get_root_resource(void);

static bool get_thread_info(zx_handle_t thread, zx_info_thread_t* info) {
    return zx_object_get_info(thread, ZX_INFO_THREAD, info, sizeof(*info), NULL, NULL) == ZX_OK;
}

static bool wait_thread(zx_handle_t thread, uint32_t reason) {
    while (true) {
        zx_info_thread_t info;
        ASSERT_TRUE(get_thread_info(thread, &info), "");
        if (info.state == reason)
            break;
        zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));
    }
    return true;
}

static int interrupt_test_thread(void *arg) {
    zx_handle_t rsrc = get_root_resource();
    zx_handle_t vinth;
    ASSERT_EQ(zx_interrupt_create(rsrc, 0, ZX_INTERRUPT_VIRTUAL,
                                  &vinth), ZX_OK, "");

    while(1) {
        ASSERT_EQ(zx_interrupt_wait(vinth, NULL), ZX_OK, "");
    }
    return 0;
}

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
    ASSERT_EQ(zx_port_wait(port_handle_bind, ZX_TIME_INFINITE, &out), ZX_OK, "");
    ASSERT_EQ(out.interrupt.timestamp, signaled_timestamp_1, "");

    // Triggering 2nd time, ACKing it causes port packet to be delivered
    ASSERT_EQ(zx_interrupt_trigger(virt_interrupt_port_handle, 0, signaled_timestamp_1), ZX_OK, "");
    ASSERT_EQ(zx_interrupt_ack(virt_interrupt_port_handle), ZX_OK, "");
    ASSERT_EQ(zx_port_wait(port_handle_bind, ZX_TIME_INFINITE, &out), ZX_OK, "");
    ASSERT_EQ(out.interrupt.timestamp, signaled_timestamp_1, "");
    ASSERT_EQ(out.key, key, "");
    ASSERT_EQ(out.type, ZX_PKT_TYPE_INTERRUPT, "");
    ASSERT_EQ(out.status, ZX_OK, "");
    ASSERT_EQ(zx_interrupt_ack(virt_interrupt_port_handle), ZX_OK, "");

    // Triggering it twice
    // the 2nd timestamp is recorded and upon ACK another packet is queued
    ASSERT_EQ(zx_interrupt_trigger(virt_interrupt_port_handle, 0, signaled_timestamp_1), ZX_OK, "");
    ASSERT_EQ(zx_interrupt_trigger(virt_interrupt_port_handle, 0, signaled_timestamp_2), ZX_OK, "");
    ASSERT_EQ(zx_port_wait(port_handle_bind, ZX_TIME_INFINITE, &out), ZX_OK, "");
    ASSERT_EQ(out.interrupt.timestamp, signaled_timestamp_1, "");
    ASSERT_EQ(zx_interrupt_ack(virt_interrupt_port_handle), ZX_OK, "");
    ASSERT_EQ(zx_port_wait(port_handle_bind, ZX_TIME_INFINITE, &out), ZX_OK, "");
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

// Tests interrupt thread after suspend/resume
static bool interrupt_suspend_test(void) {
    BEGIN_TEST;

    zx_handle_t thread_h;
    const char* thread_name = "interrupt_test_thread";
    // preallocated stack to satisfy the thread we create
    static uint8_t stack[1024] __ALIGNED(16);


    // Create and start a thread which waits for an IRQ
    ASSERT_EQ(zx_thread_create(zx_process_self(), thread_name, strlen(thread_name),
                               0, &thread_h), ZX_OK, "");

    ASSERT_EQ(zx_thread_start(thread_h, (uintptr_t)interrupt_test_thread,
                             (uintptr_t)stack + sizeof(stack),
                             0, 0), ZX_OK, "");

    // Wait till the thread is in blocked state
    ASSERT_TRUE(wait_thread(thread_h, ZX_THREAD_STATE_BLOCKED_INTERRUPT), "");

    // Suspend the thread, wait till it is suspended
    zx_handle_t suspend_token = ZX_HANDLE_INVALID;
    ASSERT_EQ(zx_task_suspend_token(thread_h, &suspend_token), ZX_OK, "");
    ASSERT_TRUE(wait_thread(thread_h, ZX_THREAD_STATE_SUSPENDED), "");

    // Resume the thread, wait till it is back to being in blocked state
    ASSERT_EQ(zx_handle_close(suspend_token), ZX_OK, "");
    ASSERT_TRUE(wait_thread(thread_h, ZX_THREAD_STATE_BLOCKED_INTERRUPT), "");

    END_TEST;
}

BEGIN_TEST_CASE(interrupt_tests)
RUN_TEST(interrupt_test)
RUN_TEST(interrupt_port_bound_test)
RUN_TEST(interrupt_port_non_bindable_test)
RUN_TEST(interrupt_suspend_test)
END_TEST_CASE(interrupt_tests)
