// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>

#include <arch/hypervisor.h>

namespace {

bool vmlaunch_fail() {
  BEGIN_TEST;

  // Create a Guest object, which will both determine if VMX is supported, and
  // set up the CPU state correctly if so.
  auto guest = NormalGuest::Create();
  if (guest.is_error() && guest.status_value() == ZX_ERR_NOT_SUPPORTED) {
    printf("VMX not supported. Skipping test.\n");
    return true;
  }
  ASSERT_TRUE(guest.is_ok());

  // Attempt to launch an empty VMCS state.
  //
  // The state is invalid and we haven't performed a "vmptrld" on it, so
  // the vmlaunch will fail. However, the pointers are all valid, so we
  // shouldn't fault, but gracefully return ZX_ERR_INTERNAL.
  VmxState state = {};
  interrupt_saved_state_t interrupt_state = arch_interrupt_save();
  zx_status_t status = vmx_enter(&state);
  arch_interrupt_restore(interrupt_state);
  EXPECT_EQ(status, ZX_ERR_INTERNAL);

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(x86_vmx)
UNITTEST("Exercise the error path when a vmlaunch fails", vmlaunch_fail)
UNITTEST_END_TESTCASE(x86_vmx, "x86-vmx", "x86-specific VMX unit tests")
