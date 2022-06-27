// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/port.h>
#include <lib/zx/resource.h>
#include <lib/zx/thread.h>
#include <lib/zx/vcpu.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/syscalls/port.h>
#include <zircon/threads.h>

#include <future>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "constants.h"
#include "hypervisor_tests.h"

namespace {

DECLARE_TEST_FUNCTION(vcpu_enter)
DECLARE_TEST_FUNCTION(vcpu_wait)
DECLARE_TEST_FUNCTION(vcpu_always_exit)
DECLARE_TEST_FUNCTION(guest_set_trap)

TEST(Guest, VcpuEnter) {
  TestCase test;
  ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, vcpu_enter_start, vcpu_enter_end));

  ASSERT_NO_FATAL_FAILURE(EnterAndCleanExit(&test));
}

TEST(Guest, VcpuKick) {
  TestCase test;
  std::promise<void> barrier;
  auto future = barrier.get_future();

  // Create and run a VCPU on a different thread.
  std::thread thread([&test, barrier = std::move(barrier)]() mutable {
    ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, vcpu_wait_start, vcpu_wait_end));
    barrier.set_value();
    zx_port_packet_t packet = {};
    ASSERT_EQ(test.vcpu.enter(&packet), ZX_ERR_CANCELED);
  });

  future.wait();

  ASSERT_EQ(test.vcpu.kick(), ZX_OK);
  thread.join();
}

TEST(Guest, VcpuSuspendThread) {
  TestCase test;
  std::atomic<bool> thread_suspended = false;
  std::promise<void> barrier;
  auto future = barrier.get_future();

  // Create and run a VCPU on a different thread.
  std::thread std_thread([&test, &thread_suspended, barrier = std::move(barrier)]() mutable {
    ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, vcpu_wait_start, vcpu_wait_end));
    barrier.set_value();
    zx_port_packet_t packet = {};
    ASSERT_EQ(test.vcpu.enter(&packet), ZX_ERR_CANCELED);
    EXPECT_TRUE(thread_suspended.load());
  });

  future.wait();

  // Suspend the thread the VCPU is being run on.
  zx::unowned_thread thread(native_thread_get_zx_handle(std_thread.native_handle()));
  zx::suspend_token token;
  ASSERT_EQ(thread->suspend(&token), ZX_OK);
  zx_signals_t pending;
  ASSERT_EQ(thread->wait_one(ZX_THREAD_SUSPENDED, zx::time::infinite(), &pending), ZX_OK);
  EXPECT_EQ(pending & ZX_THREAD_SUSPENDED, ZX_THREAD_SUSPENDED);
  thread_suspended.store(true);
  token.reset();

  ASSERT_EQ(test.vcpu.kick(), ZX_OK);
  std_thread.join();
}

TEST(Guest, VcpuProcessDestruction) {
  std::promise<void> barrier;
  auto future = barrier.get_future();

  std::thread thread([barrier = std::move(barrier)]() mutable {
    TestCase test;
    ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, vcpu_wait_start, vcpu_wait_end));
    barrier.set_value();
    zx_port_packet_t packet = {};
    ASSERT_EQ(test.vcpu.enter(&packet), ZX_ERR_CANCELED);
  });

  future.wait();

  // Detach the thread that is running the VCPU. The VCPU will be destroyed via
  // process destruction in the kernel. This verifies the kernel does not panic,
  // and correctly handles the condition.
  thread.detach();
}

TEST(Guest, VcpuInvalidThreadReuse) {
  {
    TestCase test;
    ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, vcpu_enter_start, vcpu_enter_end));

    zx::vcpu vcpu;
    zx_status_t status = zx::vcpu::create(test.guest, 0, 0, &vcpu);
    ASSERT_EQ(status, ZX_ERR_BAD_STATE);
  }

  TestCase test;
  ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, vcpu_enter_start, vcpu_enter_end));
}

TEST(Guest, GuestSetTrapWithMem) {
  TestCase test;
  ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, guest_set_trap_start, guest_set_trap_end));

  // Trap on access of TRAP_ADDR.
  ASSERT_EQ(test.guest.set_trap(ZX_GUEST_TRAP_MEM, TRAP_ADDR, PAGE_SIZE, zx::port(), kTrapKey),
            ZX_OK);

  zx_port_packet_t packet = {};
  ASSERT_EQ(test.vcpu.enter(&packet), ZX_OK);
  EXPECT_EQ(packet.key, kTrapKey);
  EXPECT_EQ(packet.type, ZX_PKT_TYPE_GUEST_MEM);

  ASSERT_NO_FATAL_FAILURE(EnterAndCleanExit(&test));
}

TEST(Guest, GuestSetTrapWithBell) {
  TestCase test;
  ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, guest_set_trap_start, guest_set_trap_end));

  zx::port port;
  ASSERT_EQ(zx::port::create(0, &port), ZX_OK);

  // Trap on access of TRAP_ADDR.
  ASSERT_EQ(test.guest.set_trap(ZX_GUEST_TRAP_BELL, TRAP_ADDR, PAGE_SIZE, port, kTrapKey), ZX_OK);

  ASSERT_NO_FATAL_FAILURE(EnterAndCleanExit(&test));

  zx_port_packet_t packet = {};
  ASSERT_EQ(port.wait(zx::time::infinite(), &packet), ZX_OK);
  EXPECT_EQ(packet.key, kTrapKey);
  EXPECT_EQ(packet.type, ZX_PKT_TYPE_GUEST_BELL);
  EXPECT_EQ(packet.guest_bell.addr, static_cast<zx_gpaddr_t>(TRAP_ADDR));
}

// TestCase for fxbug.dev/33986.
TEST(Guest, GuestSetTrapWithBellDrop) {
  // Build the port before test so test is destructed first.
  zx::port port;
  ASSERT_EQ(zx::port::create(0, &port), ZX_OK);

  TestCase test;
  ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, guest_set_trap_start, guest_set_trap_end));

  // Trap on access of TRAP_ADDR.
  ASSERT_EQ(test.guest.set_trap(ZX_GUEST_TRAP_BELL, TRAP_ADDR, PAGE_SIZE, port, kTrapKey), ZX_OK);

  ASSERT_NO_FATAL_FAILURE(EnterAndCleanExit(&test));

  // The guest in test is destructed with one packet still queued on the
  // port. This should work correctly.
}

// TestCase for fxbug.dev/34001.
TEST(Guest, GuestSetTrapWithBellAndUser) {
  zx::port port;
  ASSERT_EQ(zx::port::create(0, &port), ZX_OK);

  // Queue a packet with the same key as the trap.
  zx_port_packet packet = {};
  packet.key = kTrapKey;
  packet.type = ZX_PKT_TYPE_USER;
  ASSERT_EQ(port.queue(&packet), ZX_OK);

  // Force guest to be released and cancel all packets associated with traps.
  {
    TestCase test;
    ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, guest_set_trap_start, guest_set_trap_end));

    // Trap on access of TRAP_ADDR.
    ASSERT_EQ(test.guest.set_trap(ZX_GUEST_TRAP_BELL, TRAP_ADDR, PAGE_SIZE, port, kTrapKey), ZX_OK);

    ASSERT_NO_FATAL_FAILURE(EnterAndCleanExit(&test));
  }

  ASSERT_EQ(port.wait(zx::time::infinite(), &packet), ZX_OK);
  EXPECT_EQ(packet.key, kTrapKey);
  EXPECT_EQ(packet.type, ZX_PKT_TYPE_USER);
}

// See that zx::vcpu::enter returns ZX_ERR_BAD_STATE if the port has been closed.
TEST(Guest, GuestSetTrapClosePort) {
  zx::port port;
  ASSERT_EQ(zx::port::create(0, &port), ZX_OK);

  TestCase test;
  ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, guest_set_trap_start, guest_set_trap_end));

  ASSERT_EQ(test.guest.set_trap(ZX_GUEST_TRAP_BELL, TRAP_ADDR, PAGE_SIZE, port, kTrapKey), ZX_OK);

  port.reset();

  zx_port_packet_t packet = {};
  ASSERT_EQ(test.vcpu.enter(&packet), ZX_ERR_BAD_STATE);

  ASSERT_NO_FATAL_FAILURE(EnterAndCleanExit(&test));
}

TEST(Guest, VcpuUseAfterThreadExits) {
  TestCase test;
  zx_status_t status = ZX_ERR_NOT_SUPPORTED;
  // Do the setup on another thread so that the VCPU attaches to the other thread.
  std::thread t([&]() {
    ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, vcpu_enter_start, vcpu_enter_end));
    status = ZX_OK;
  });
  t.join();

  ASSERT_EQ(status, ZX_OK);
  // Send an interrupt to the VCPU after the thread has been shutdown.
  test.vcpu.interrupt(kInterruptVector);
  // Shutdown the VCPU after the thread has been shutdown.
  test.vcpu.reset();
}

// Delete a VCPU from a thread different to the one it last ran on.
TEST(Guest, VcpuDeleteFromOtherThread) {
  TestCase test;
  std::promise<void> ready_barrier;
  auto ready_future = ready_barrier.get_future();
  std::promise<void> exit_barrier;

  // Create and run a VCPU on a different thread.
  std::thread t([&test, ready_barrier = std::move(ready_barrier),
                 exit_future = exit_barrier.get_future()]() mutable {
    // Start the guest.
    ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, vcpu_always_exit_start, vcpu_always_exit_end));
    ASSERT_EQ(test.guest.set_trap(ZX_GUEST_TRAP_MEM, TRAP_ADDR, PAGE_SIZE, zx::port(), kTrapKey),
              ZX_OK);

    // Run the guest a few times to ensure all kernel state relating to
    // the guest has been fully initialized (and hence must be torn down
    // when we delete the VCPU below).
    for (int i = 0; i < 3; i++) {
      zx_port_packet_t packet;
      test.vcpu.enter(&packet);
    }
    ready_barrier.set_value();

    // Don't exit until the main thread has completed its test.
    exit_future.wait();
  });

  // Wait for the child thread to start running its guest.
  ready_future.wait();

  // Delete the VCPU.
  test.vcpu.reset();

  // Stop the child thread.
  exit_barrier.set_value();
  t.join();
}

}  // namespace
