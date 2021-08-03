// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_TESTS_HYPERVISOR_HYPERVISOR_TESTS_H_
#define SRC_VIRTUALIZATION_TESTS_HYPERVISOR_HYPERVISOR_TESTS_H_

#include <lib/zx/channel.h>
#include <lib/zx/guest.h>
#include <lib/zx/port.h>
#include <lib/zx/resource.h>
#include <lib/zx/vcpu.h>
#include <lib/zx/vmar.h>
#include <zircon/types.h>

#include "constants.h"

constexpr uint32_t kGuestMapFlags =
    ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_PERM_EXECUTE | ZX_VM_SPECIFIC;
constexpr uint32_t kHostMapFlags = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;
// Inject an interrupt with vector 32, the first user defined interrupt vector.
constexpr uint32_t kInterruptVector = 32u;
constexpr uint64_t kTrapKey = 0x1234;

// Declare symbols for the start and end of an assembly function.
#define DECLARE_TEST_FUNCTION(name)     \
  extern "C" const char name##_start[]; \
  extern "C" const char name##_end[];

struct TestCase {
  bool interrupts_enabled = false;
  uintptr_t host_addr = 0;

  zx::vmo vmo;
  zx::guest guest;
  zx::vmar vmar;
  zx::vcpu vcpu;

  ~TestCase() {
    if (host_addr != 0) {
      zx::vmar::root_self()->unmap(host_addr, VMO_SIZE);
    }
  }
};

// Setup a guest environment consisting of the code between `start` and `end`.
//
// Flags a gUnit error on failure.
void SetupGuest(TestCase* test, const char* start, const char* end);

// Resume the guest, and ensure it exits by touching the memory at EXIT_TEST_ADDR.
//
// Flags a gUnit error on failure.
void ResumeAndCleanExit(TestCase* test);

#endif  // SRC_VIRTUALIZATION_TESTS_HYPERVISOR_HYPERVISOR_TESTS_H_
