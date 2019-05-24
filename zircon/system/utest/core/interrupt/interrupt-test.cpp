// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/guest.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/port.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <lib/zx/vcpu.h>
#include <zxtest/zxtest.h>

namespace {

extern "C" zx_handle_t get_root_resource(void);

constexpr zx::time kSignaledTimeStamp1(12345);
constexpr zx::time kSignaledTimeStamp2(67890);
constexpr uint32_t kKey = 789;

class RootResourceFixture : public zxtest::Test {
    public:
    // Please do not use get_root_resource() in new code. See ZX-1467.
    void SetUp() override { root_resource_ = zx::unowned_resource(get_root_resource()); }
    protected:
    zx::unowned_resource root_resource_;
};

// Use an alias so we use a different test case name.
using InterruptTest = RootResourceFixture;

bool WaitThread(const zx::thread& thread, uint32_t reason) {
    while (true) {
        zx_info_thread_t info;
        EXPECT_OK(thread.get_info(ZX_INFO_THREAD, &info, sizeof(info), nullptr, nullptr));
        if (info.state == reason) {
            return true;
        }
        zx::nanosleep(zx::deadline_after(zx::msec(1)));
    }
    return true;
}

void ThreadEntry(uintptr_t arg1, uintptr_t arg2) {
    zx_handle_t interrupt = static_cast<zx_handle_t>(arg1);
    while (zx_interrupt_wait(interrupt, nullptr) == ZX_OK) {}
}

// Tests to bind interrupt to a non-bindable port
TEST_F(InterruptTest, NonBindablePort) {
    zx::interrupt interrupt;
    zx::port port;

    ASSERT_OK(zx::interrupt::create(*root_resource_, 0, ZX_INTERRUPT_VIRTUAL, &interrupt));
    // Incorrectly pass 0 for options instead of ZX_PORT_BIND_TO_INTERRUPT
    ASSERT_OK(zx::port::create(0, &port));

    ASSERT_EQ(interrupt.bind(port, kKey, 0), ZX_ERR_WRONG_TYPE);
}

// Tests Interrupts bound to a port
TEST_F(InterruptTest, BindPort) {
    zx::interrupt interrupt;
    zx::port port;
    zx_port_packet_t out;

    ASSERT_OK(zx::interrupt::create(*root_resource_, 0, ZX_INTERRUPT_VIRTUAL, &interrupt));
    ASSERT_OK(zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &port));

    // Test port binding
    ASSERT_OK(interrupt.bind(port, kKey, 0));
    ASSERT_OK(interrupt.trigger(0, kSignaledTimeStamp1));
    ASSERT_OK(port.wait(zx::time::infinite(), &out));
    ASSERT_EQ(out.interrupt.timestamp, kSignaledTimeStamp1.get());

    // Triggering 2nd time, ACKing it causes port packet to be delivered
    ASSERT_OK(interrupt.trigger(0, kSignaledTimeStamp1));
    ASSERT_OK(interrupt.ack());
    ASSERT_OK(port.wait(zx::time::infinite(), &out));
    ASSERT_EQ(out.interrupt.timestamp, kSignaledTimeStamp1.get());
    ASSERT_EQ(out.key, kKey);
    ASSERT_EQ(out.type, ZX_PKT_TYPE_INTERRUPT);
    ASSERT_OK(out.status);
    ASSERT_OK(interrupt.ack());

    // Triggering it twice
    // the 2nd timestamp is recorded and upon ACK another packet is queued
    ASSERT_OK(interrupt.trigger(0, kSignaledTimeStamp1));
    ASSERT_OK(interrupt.trigger(0, kSignaledTimeStamp2));
    ASSERT_OK(port.wait(zx::time::infinite(), &out));
    ASSERT_EQ(out.interrupt.timestamp, kSignaledTimeStamp1.get());
    ASSERT_OK(interrupt.ack());
    ASSERT_OK(port.wait(zx::time::infinite(), &out));
    ASSERT_EQ(out.interrupt.timestamp, kSignaledTimeStamp2.get());

    // Try to destroy now, expecting to return error telling packet
    // has been read but the interrupt has not been re-armed
    ASSERT_EQ(interrupt.destroy(), ZX_ERR_NOT_FOUND);
    ASSERT_EQ(interrupt.ack(), ZX_ERR_CANCELED);
    ASSERT_EQ(interrupt.trigger(0, kSignaledTimeStamp1), ZX_ERR_CANCELED);
}

// Tests support for virtual interrupts
TEST_F(InterruptTest, VirtualInterrupts) {
    zx::interrupt interrupt;
    zx::interrupt interrupt_cancelled;
    zx::time timestamp;

    ASSERT_EQ(zx::interrupt::create(*root_resource_, 0, ZX_INTERRUPT_SLOT_USER, &interrupt),
              ZX_ERR_INVALID_ARGS);
    ASSERT_OK(zx::interrupt::create(*root_resource_, 0, ZX_INTERRUPT_VIRTUAL, &interrupt));
    ASSERT_OK(zx::interrupt::create(*root_resource_, 0, ZX_INTERRUPT_VIRTUAL, &interrupt_cancelled));

    ASSERT_OK(interrupt_cancelled.destroy());
    ASSERT_EQ(interrupt_cancelled.trigger(0, kSignaledTimeStamp1), ZX_ERR_CANCELED);

    ASSERT_OK(interrupt.trigger(0, kSignaledTimeStamp1));

    ASSERT_EQ(interrupt_cancelled.wait(&timestamp), ZX_ERR_CANCELED);
    ASSERT_OK(interrupt.wait(&timestamp));
    ASSERT_EQ(timestamp.get(), kSignaledTimeStamp1.get());

    ASSERT_OK(interrupt.trigger(0, kSignaledTimeStamp1));
    ASSERT_OK(interrupt.wait(&timestamp));
}

// Tests interrupt thread after suspend/resume
TEST_F(InterruptTest, WaitThreadFunctionsAfterSuspendResume) {
    zx::interrupt interrupt;
    zx::thread thread;
    constexpr char name[] = "interrupt_test_thread";
    // preallocated stack to satisfy the thread we create
    static uint8_t stack[1024] __ALIGNED(16);

    ASSERT_OK(zx::interrupt::create(*root_resource_, 0, ZX_INTERRUPT_VIRTUAL, &interrupt));

    // Create and start a thread which waits for an IRQ
    ASSERT_OK(zx::thread::create(*zx::process::self(), name, sizeof(name), 0, &thread));

    ASSERT_OK(thread.start(reinterpret_cast<uintptr_t>(ThreadEntry),
                           reinterpret_cast<uintptr_t>(stack) + sizeof(stack),
                           static_cast<uintptr_t>(interrupt.get()), 0));

    // Wait till the thread is in blocked state
    ASSERT_TRUE(WaitThread(thread, ZX_THREAD_STATE_BLOCKED_INTERRUPT));

    // Suspend the thread, wait till it is suspended
    zx::suspend_token suspend_token;
    ASSERT_OK(thread.suspend(&suspend_token));
    ASSERT_TRUE(WaitThread(thread, ZX_THREAD_STATE_SUSPENDED));

    // Resume the thread, wait till it is back to being in blocked state
    suspend_token.reset();
    ASSERT_TRUE(WaitThread(thread, ZX_THREAD_STATE_BLOCKED_INTERRUPT));
    thread.kill();

    // Wait for termination to reduce interference with subsequent tests.
    zx_signals_t observed;
    ASSERT_OK(thread.wait_one(ZX_TASK_TERMINATED, zx::time::infinite(), &observed));
}

// Tests binding an interrupt to multiple VCPUs
TEST_F(InterruptTest, BindVcpuTest) {
    zx::interrupt interrupt;
    zx::guest guest;
    zx::vmar vmar;
    zx::vcpu vcpu1;
    zx::vcpu vcpu2;

    zx_status_t status = zx::guest::create(*root_resource_, 0, &guest, &vmar);
    if (status == ZX_ERR_NOT_SUPPORTED) {
        fprintf(stderr, "Guest creation not supported\n");
        return;
    }
    ASSERT_OK(status);

    ASSERT_OK(zx::interrupt::create(*root_resource_, 0, 0, &interrupt));
    ASSERT_OK(zx::vcpu::create(guest, 0, 0, &vcpu1));
    ASSERT_OK(zx::vcpu::create(guest, 0, 0, &vcpu2));

    ASSERT_OK(interrupt.bind_vcpu(vcpu1, 0));
    ASSERT_OK(interrupt.bind_vcpu(vcpu2, 0));
}

// Tests binding a virtual interrupt to a VCPU
TEST_F(InterruptTest, UnableToBindVirtualToVcpuAfterPort) {
    zx::interrupt interrupt;
    zx::port port;
    zx::guest guest;
    zx::vmar vmar;
    zx::vcpu vcpu;

    zx_status_t status = zx::guest::create(*root_resource_, 0, &guest, &vmar);
    if (status == ZX_ERR_NOT_SUPPORTED) {
        fprintf(stderr, "Guest creation not supported\n");
        return;
    }
    ASSERT_OK(status);

    ASSERT_OK(zx::interrupt::create(*root_resource_, 0, ZX_INTERRUPT_VIRTUAL, &interrupt));
    ASSERT_OK(zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &port));
    ASSERT_OK(zx::vcpu::create(guest, 0, 0, &vcpu));

    ASSERT_OK(interrupt.bind(port, 0, 0));
    ASSERT_EQ(interrupt.bind_vcpu(vcpu, 0), ZX_ERR_NOT_SUPPORTED);
}

// Tests binding an interrupt to a VCPU, after binding it to a port
TEST_F(InterruptTest, UnableToBindToVcpuAfterPort) {
    zx::interrupt interrupt;
    zx::port port;
    zx::guest guest;
    zx::vmar vmar;
    zx::vcpu vcpu;

    zx_status_t status = zx::guest::create(*root_resource_, 0, &guest, &vmar);
    if (status == ZX_ERR_NOT_SUPPORTED) {
        fprintf(stderr, "Guest creation not supported\n");
        return;
    }
    ASSERT_OK(status);

    ASSERT_OK(zx::interrupt::create(*root_resource_, 0, 0, &interrupt));
    ASSERT_OK(zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &port));
    ASSERT_OK(zx::vcpu::create(guest, 0, 0, &vcpu));

    ASSERT_OK(interrupt.bind(port, 0, 0));
    ASSERT_EQ(interrupt.bind_vcpu(vcpu, 0), ZX_ERR_ALREADY_BOUND);
}

// Tests binding an interrupt to VCPUs from different guests
TEST_F(InterruptTest, UnableToBindVcpuMultipleGuests) {
    zx::interrupt interrupt;
    zx::guest guest1;
    zx::guest guest2;
    zx::vmar vmar1;
    zx::vmar vmar2;
    zx::vcpu vcpu1;
    zx::vcpu vcpu2;

    zx_status_t status = zx::guest::create(*root_resource_, 0, &guest1, &vmar1);
    if (status == ZX_ERR_NOT_SUPPORTED) {
        fprintf(stderr, "Guest creation not supported\n");
        return;
    }
    ASSERT_OK(status);

    ASSERT_OK(zx::interrupt::create(*root_resource_, 0, 0, &interrupt));
    ASSERT_OK(zx::vcpu::create(guest1, 0, 0, &vcpu1));
    ASSERT_OK(zx::guest::create(*root_resource_, 0, &guest2, &vmar2));
    ASSERT_OK(zx::vcpu::create(guest2, 0, 0, &vcpu2));

    ASSERT_OK(interrupt.bind_vcpu(vcpu1, 0));
    ASSERT_EQ(interrupt.bind_vcpu(vcpu2, 0), ZX_ERR_INVALID_ARGS);
}

} // namespace
