// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/guest.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/port.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <lib/zx/vcpu.h>
#include <unittest/unittest.h>

extern "C" zx_handle_t get_root_resource(void);

static bool get_thread_info(zx_handle_t thread, zx_info_thread_t* info) {
    return zx_object_get_info(thread, ZX_INFO_THREAD, info, sizeof(*info), NULL, NULL) == ZX_OK;
}

static bool wait_thread(const zx::thread& thread, uint32_t reason) {
    while (true) {
        zx_info_thread_t info;
        ASSERT_EQ(thread.get_info(ZX_INFO_THREAD, &info, sizeof(info), nullptr, nullptr), ZX_OK);
        if (info.state == reason) {
            return true;
        }
        zx::nanosleep(zx::deadline_after(zx::msec(1)));
    }
}

static void thread_entry(uintptr_t arg1, uintptr_t arg2) {
    zx_handle_t interrupt = static_cast<zx_handle_t>(arg1);
    while (zx_interrupt_wait(interrupt, nullptr) == ZX_OK) {}
}

// Tests to bind interrupt to a non-bindable port
static bool interrupt_port_non_bindable_test() {
    BEGIN_TEST;

    // Please do not use get_root_resource() in new code. See ZX-1497.
    zx::unowned_resource resource(get_root_resource());
    zx::interrupt interrupt;
    zx::port port;
    const uint32_t key = 789;

    ASSERT_EQ(zx::interrupt::create(*resource, 0, ZX_INTERRUPT_VIRTUAL, &interrupt), ZX_OK);
    ASSERT_EQ(zx::port::create(0, &port), ZX_OK);

    ASSERT_EQ(interrupt.bind(port, key, 0), ZX_ERR_WRONG_TYPE);

    END_TEST;
}

// Tests Interrupts bound to a port
static bool interrupt_port_bound_test() {
    BEGIN_TEST;

    // Please do not use get_root_resource() in new code. See ZX-1497.
    zx::unowned_resource resource(get_root_resource());
    zx::interrupt interrupt;
    zx::port port;
    const zx::time signaled_timestamp_1(12345);
    const zx::time signaled_timestamp_2(67890);
    const uint32_t key = 789;
    zx_port_packet_t out;

    ASSERT_EQ(zx::interrupt::create(*resource, 0, ZX_INTERRUPT_VIRTUAL, &interrupt), ZX_OK);
    ASSERT_EQ(zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &port), ZX_OK);

    // Test port binding
    ASSERT_EQ(interrupt.bind(port, key, 0), ZX_OK);
    ASSERT_EQ(interrupt.trigger(0, signaled_timestamp_1), ZX_OK);
    ASSERT_EQ(port.wait(zx::time::infinite(), &out), ZX_OK);
    ASSERT_EQ(out.interrupt.timestamp, signaled_timestamp_1.get());

    // Triggering 2nd time, ACKing it causes port packet to be delivered
    ASSERT_EQ(interrupt.trigger(0, signaled_timestamp_1), ZX_OK);
    ASSERT_EQ(interrupt.ack(), ZX_OK);
    ASSERT_EQ(port.wait(zx::time::infinite(), &out), ZX_OK);
    ASSERT_EQ(out.interrupt.timestamp, signaled_timestamp_1.get());
    ASSERT_EQ(out.key, key);
    ASSERT_EQ(out.type, ZX_PKT_TYPE_INTERRUPT);
    ASSERT_EQ(out.status, ZX_OK);
    ASSERT_EQ(interrupt.ack(), ZX_OK);

    // Triggering it twice
    // the 2nd timestamp is recorded and upon ACK another packet is queued
    ASSERT_EQ(interrupt.trigger(0, signaled_timestamp_1), ZX_OK);
    ASSERT_EQ(interrupt.trigger(0, signaled_timestamp_2), ZX_OK);
    ASSERT_EQ(port.wait(zx::time::infinite(), &out), ZX_OK);
    ASSERT_EQ(out.interrupt.timestamp, signaled_timestamp_1.get());
    ASSERT_EQ(interrupt.ack(), ZX_OK);
    ASSERT_EQ(port.wait(zx::time::infinite(), &out), ZX_OK);
    ASSERT_EQ(out.interrupt.timestamp, signaled_timestamp_2.get());

    // Try to destroy now, expecting to return error telling packet
    // has been read but the interrupt has not been re-armed
    ASSERT_EQ(interrupt.destroy(), ZX_ERR_NOT_FOUND,"");
    ASSERT_EQ(interrupt.ack(), ZX_ERR_CANCELED);
    ASSERT_EQ(interrupt.trigger(0, signaled_timestamp_1), ZX_ERR_CANCELED);

    END_TEST;
}

// Tests support for virtual interrupts
static bool interrupt_test() {
    BEGIN_TEST;

    // Please do not use get_root_resource() in new code. See ZX-1497.
    zx::unowned_resource resource(get_root_resource());
    zx::interrupt interrupt;
    zx::interrupt interrupt_cancelled;
    const zx::time signaled_timestamp(12345);
    zx::time timestamp;

    ASSERT_EQ(zx::interrupt::create(*resource, 0, ZX_INTERRUPT_SLOT_USER, &interrupt),
              ZX_ERR_INVALID_ARGS);
    ASSERT_EQ(zx::interrupt::create(*resource, 0, ZX_INTERRUPT_VIRTUAL, &interrupt), ZX_OK);
    ASSERT_EQ(zx::interrupt::create(*resource, 0, ZX_INTERRUPT_VIRTUAL, &interrupt_cancelled),
              ZX_OK);

    ASSERT_EQ(interrupt_cancelled.destroy(), ZX_OK);
    ASSERT_EQ(interrupt_cancelled.trigger(0, signaled_timestamp), ZX_ERR_CANCELED);

    ASSERT_EQ(interrupt.trigger(0, signaled_timestamp), ZX_OK);

    ASSERT_EQ(interrupt_cancelled.wait(&timestamp), ZX_ERR_CANCELED);
    ASSERT_EQ(interrupt.wait(&timestamp), ZX_OK);
    ASSERT_EQ(timestamp.get(), signaled_timestamp.get());

    ASSERT_EQ(interrupt.trigger(0, signaled_timestamp), ZX_OK);
    ASSERT_EQ(interrupt.wait(&timestamp), ZX_OK);

    END_TEST;
}

// Tests interrupt thread after suspend/resume
static bool interrupt_suspend_test() {
    BEGIN_TEST;

    // Please do not use get_root_resource() in new code. See ZX-1497.
    zx::unowned_resource resource(get_root_resource());
    zx::interrupt interrupt;
    zx::thread thread;
    const char name[] = "interrupt_test_thread";
    // preallocated stack to satisfy the thread we create
    static uint8_t stack[1024] __ALIGNED(16);

    ASSERT_EQ(zx::interrupt::create(*resource, 0, ZX_INTERRUPT_VIRTUAL, &interrupt), ZX_OK);

    // Create and start a thread which waits for an IRQ
    ASSERT_EQ(zx::thread::create(*zx::process::self(), name, sizeof(name), 0, &thread), ZX_OK);

    ASSERT_EQ(thread.start(reinterpret_cast<uintptr_t>(thread_entry),
                           reinterpret_cast<uintptr_t>(stack) + sizeof(stack),
                           static_cast<uintptr_t>(interrupt.get()), 0),
              ZX_OK);

    // Wait till the thread is in blocked state
    ASSERT_TRUE(wait_thread(thread, ZX_THREAD_STATE_BLOCKED_INTERRUPT));

    // Suspend the thread, wait till it is suspended
    zx::suspend_token suspend_token;
    ASSERT_EQ(thread.suspend(&suspend_token), ZX_OK);
    ASSERT_TRUE(wait_thread(thread, ZX_THREAD_STATE_SUSPENDED));

    // Resume the thread, wait till it is back to being in blocked state
    suspend_token.reset();
    ASSERT_TRUE(wait_thread(thread, ZX_THREAD_STATE_BLOCKED_INTERRUPT));
    thread.kill();

    // Wait for termination to reduce interference with subsequent tests.
    zx_signals_t observed;
    ASSERT_EQ(thread.wait_one(ZX_TASK_TERMINATED, zx::time::infinite(), &observed), ZX_OK);

    END_TEST;
}

// Tests binding an interrupt to multiple VCPUs
static bool interrupt_bind_vcpu_test() {
    BEGIN_TEST;

    // Please do not use get_root_resource() in new code. See ZX-1497.
    zx::unowned_resource resource(get_root_resource());
    zx::interrupt interrupt;
    zx::guest guest;
    zx::vmar vmar;
    zx::vcpu vcpu1;
    zx::vcpu vcpu2;

    zx_status_t status = zx::guest::create(*resource, 0, &guest, &vmar);
    if (status == ZX_ERR_NOT_SUPPORTED) {
        fprintf(stderr, "Guest creation not supported\n");
        return true;
    }
    ASSERT_EQ(status, ZX_OK);

    ASSERT_EQ(zx::interrupt::create(*resource, 0, 0, &interrupt), ZX_OK);
    ASSERT_EQ(zx::vcpu::create(guest, 0, 0, &vcpu1), ZX_OK);
    ASSERT_EQ(zx::vcpu::create(guest, 0, 0, &vcpu2), ZX_OK);

    ASSERT_EQ(interrupt.bind_vcpu(vcpu1, 0), ZX_OK);
    ASSERT_EQ(interrupt.bind_vcpu(vcpu2, 0), ZX_OK);

    END_TEST;
}

// Tests binding a virtual interrupt to a VCPU
static bool interrupt_bind_vcpu_not_supported_test() {
    BEGIN_TEST;

    // Please do not use get_root_resource() in new code. See ZX-1497.
    zx::unowned_resource resource(get_root_resource());
    zx::interrupt interrupt;
    zx::port port;
    zx::guest guest;
    zx::vmar vmar;
    zx::vcpu vcpu;

    zx_status_t status = zx::guest::create(*resource, 0, &guest, &vmar);
    if (status == ZX_ERR_NOT_SUPPORTED) {
        fprintf(stderr, "Guest creation not supported\n");
        return true;
    }
    ASSERT_EQ(status, ZX_OK);

    ASSERT_EQ(zx::interrupt::create(*resource, 0, ZX_INTERRUPT_VIRTUAL, &interrupt), ZX_OK);
    ASSERT_EQ(zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &port), ZX_OK);
    ASSERT_EQ(zx::vcpu::create(guest, 0, 0, &vcpu), ZX_OK);

    ASSERT_EQ(interrupt.bind(port, 0, 0), ZX_OK);
    ASSERT_EQ(interrupt.bind_vcpu(vcpu, 0), ZX_ERR_NOT_SUPPORTED);

    END_TEST;
}

// Tests binding an interrupt to a VCPU, after binding it to a port
static bool interrupt_bind_vcpu_already_bound_test() {
    BEGIN_TEST;

    // Please do not use get_root_resource() in new code. See ZX-1497.
    zx::unowned_resource resource(get_root_resource());
    zx::interrupt interrupt;
    zx::port port;
    zx::guest guest;
    zx::vmar vmar;
    zx::vcpu vcpu;

    zx_status_t status = zx::guest::create(*resource, 0, &guest, &vmar);
    if (status == ZX_ERR_NOT_SUPPORTED) {
        fprintf(stderr, "Guest creation not supported\n");
        return true;
    }
    ASSERT_EQ(status, ZX_OK);

    ASSERT_EQ(zx::interrupt::create(*resource, 0, 0, &interrupt), ZX_OK);
    ASSERT_EQ(zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &port), ZX_OK);
    ASSERT_EQ(zx::vcpu::create(guest, 0, 0, &vcpu), ZX_OK);

    ASSERT_EQ(interrupt.bind(port, 0, 0), ZX_OK);
    ASSERT_EQ(interrupt.bind_vcpu(vcpu, 0), ZX_ERR_ALREADY_BOUND);

    END_TEST;
}

// Tests binding an interrupt to VCPUs from different guests
static bool interrupt_bind_vcpu_multiple_guests_test() {
    BEGIN_TEST;

    // Please do not use get_root_resource() in new code. See ZX-1497.
    zx::unowned_resource resource(get_root_resource());
    zx::interrupt interrupt;
    zx::guest guest1;
    zx::guest guest2;
    zx::vmar vmar1;
    zx::vmar vmar2;
    zx::vcpu vcpu1;
    zx::vcpu vcpu2;

    zx_status_t status = zx::guest::create(*resource, 0, &guest1, &vmar1);
    if (status == ZX_ERR_NOT_SUPPORTED) {
        fprintf(stderr, "Guest creation not supported\n");
        return true;
    }
    ASSERT_EQ(status, ZX_OK);

    ASSERT_EQ(zx::interrupt::create(*resource, 0, 0, &interrupt), ZX_OK);
    ASSERT_EQ(zx::vcpu::create(guest1, 0, 0, &vcpu1), ZX_OK);
    ASSERT_EQ(zx::guest::create(*resource, 0, &guest2, &vmar2), ZX_OK);
    ASSERT_EQ(zx::vcpu::create(guest2, 0, 0, &vcpu2), ZX_OK);

    ASSERT_EQ(interrupt.bind_vcpu(vcpu1, 0), ZX_OK);
    ASSERT_EQ(interrupt.bind_vcpu(vcpu2, 0), ZX_ERR_INVALID_ARGS);

    END_TEST;
}

BEGIN_TEST_CASE(interrupt_tests)
RUN_TEST(interrupt_test)
RUN_TEST(interrupt_port_bound_test)
RUN_TEST(interrupt_port_non_bindable_test)
RUN_TEST(interrupt_suspend_test)
RUN_TEST(interrupt_bind_vcpu_test)
RUN_TEST(interrupt_bind_vcpu_not_supported_test)
RUN_TEST(interrupt_bind_vcpu_already_bound_test)
RUN_TEST(interrupt_bind_vcpu_multiple_guests_test)
END_TEST_CASE(interrupt_tests)
