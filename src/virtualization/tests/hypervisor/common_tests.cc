// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/port.h>
#include <lib/zx/resource.h>
#include <lib/zx/vcpu.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/syscalls/port.h>

#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "constants.h"
#include "hypervisor_tests.h"

namespace {

DECLARE_TEST_FUNCTION(vcpu_resume)
DECLARE_TEST_FUNCTION(guest_set_trap)
DECLARE_TEST_FUNCTION(exiting_guest)

TEST(Guest, VcpuResume) {
  TestCase test;
  ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, vcpu_resume_start, vcpu_resume_end));

  ASSERT_NO_FATAL_FAILURE(ResumeAndCleanExit(&test));
}

TEST(Guest, VcpuInvalidThreadReuse) {
  {
    TestCase test;
    ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, vcpu_resume_start, vcpu_resume_end));

    zx::vcpu vcpu;
    zx_status_t status = zx::vcpu::create(test.guest, 0, 0, &vcpu);
    ASSERT_EQ(status, ZX_ERR_BAD_STATE);
  }

  TestCase test;
  ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, vcpu_resume_start, vcpu_resume_end));
}

TEST(Guest, GuestSetTrapWithMem) {
  TestCase test;
  ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, guest_set_trap_start, guest_set_trap_end));

  // Trap on access of TRAP_ADDR.
  ASSERT_EQ(test.guest.set_trap(ZX_GUEST_TRAP_MEM, TRAP_ADDR, PAGE_SIZE, zx::port(), kTrapKey),
            ZX_OK);

  zx_port_packet_t packet = {};
  ASSERT_EQ(test.vcpu.resume(&packet), ZX_OK);
  EXPECT_EQ(packet.key, kTrapKey);
  EXPECT_EQ(packet.type, ZX_PKT_TYPE_GUEST_MEM);

  ASSERT_NO_FATAL_FAILURE(ResumeAndCleanExit(&test));
}

TEST(Guest, GuestSetTrapWithBell) {
  TestCase test;
  ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, guest_set_trap_start, guest_set_trap_end));

  zx::port port;
  ASSERT_EQ(zx::port::create(0, &port), ZX_OK);

  // Trap on access of TRAP_ADDR.
  ASSERT_EQ(test.guest.set_trap(ZX_GUEST_TRAP_BELL, TRAP_ADDR, PAGE_SIZE, port, kTrapKey), ZX_OK);

  ASSERT_NO_FATAL_FAILURE(ResumeAndCleanExit(&test));

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

  ASSERT_NO_FATAL_FAILURE(ResumeAndCleanExit(&test));

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

    ASSERT_NO_FATAL_FAILURE(ResumeAndCleanExit(&test));
  }

  ASSERT_EQ(port.wait(zx::time::infinite(), &packet), ZX_OK);
  EXPECT_EQ(packet.key, kTrapKey);
  EXPECT_EQ(packet.type, ZX_PKT_TYPE_USER);
}

// See that zx::vcpu::resume returns ZX_ERR_BAD_STATE if the port has been closed.
TEST(Guest, GuestSetTrapClosePort) {
  zx::port port;
  ASSERT_EQ(zx::port::create(0, &port), ZX_OK);

  TestCase test;
  ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, guest_set_trap_start, guest_set_trap_end));

  ASSERT_EQ(test.guest.set_trap(ZX_GUEST_TRAP_BELL, TRAP_ADDR, PAGE_SIZE, port, kTrapKey), ZX_OK);

  port.reset();

  zx_port_packet_t packet = {};
  ASSERT_EQ(test.vcpu.resume(&packet), ZX_ERR_BAD_STATE);

  ASSERT_NO_FATAL_FAILURE(ResumeAndCleanExit(&test));
}

TEST(Guest, VcpuUseAfterThreadExits) {
  TestCase test;
  zx_status_t status = ZX_ERR_NOT_SUPPORTED;
  // Do the setup on another thread so that the VCPU attaches to the other thread.
  std::thread t([&]() {
    ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, vcpu_resume_start, vcpu_resume_end));
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
  std::atomic<bool> child_ready = false;
  std::atomic<bool> child_should_exit = false;

  TestCase test;

  // Create and run a VCPU on a different thread.
  std::thread t([&test, &child_ready, &child_should_exit]() {
    // Start the guest.
    ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, exiting_guest_start, exiting_guest_end));
    ASSERT_EQ(test.guest.set_trap(ZX_GUEST_TRAP_MEM, TRAP_ADDR, PAGE_SIZE, zx::port(), kTrapKey),
              ZX_OK);

    // Run the guest a few times to ensure all kernel state relating to
    // the guest has been fully initialized (and hence must be torn down
    // when we delete the VCPU below).
    for (int i = 0; i < 3; i++) {
      zx_port_packet_t packet;
      test.vcpu.resume(&packet);
    }
    child_ready.store(true);

    // Don't exit until the main thread has completed its test.
    while (!child_should_exit.load()) {
    }
  });

  // Wait for the child thread to start running its guest.
  while (!child_ready.load()) {
  }

  // Delete the VCPU.
  test.vcpu.reset();

  // Stop the child thread.
  child_should_exit.store(true);
  t.join();
}

}  // namespace
