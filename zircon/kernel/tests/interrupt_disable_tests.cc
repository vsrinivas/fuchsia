// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>

#include <arch/interrupt.h>
#include <arch/ops.h>

static bool interrupt_disable_test() {
  BEGIN_TEST;

  // make sure ints are disabled and that a simple enable/disable tracks
  ASSERT_EQ(false, arch_ints_disabled());
  arch_disable_ints();
  ASSERT_EQ(true, arch_ints_disabled());
  arch_enable_ints();
  ASSERT_EQ(false, arch_ints_disabled());

  END_TEST;
}

static bool interrupt_save_restore_test() {
  BEGIN_TEST;

  // validate that a simple save/restore works
  {
    ASSERT_EQ(false, arch_ints_disabled());
    interrupt_saved_state_t state = arch_interrupt_save();
    ASSERT_EQ(true, arch_ints_disabled());
    arch_interrupt_restore(state);
    ASSERT_EQ(false, arch_ints_disabled());
  }

  // validate that a nested save/restore works
  {
    ASSERT_EQ(false, arch_ints_disabled());
    interrupt_saved_state_t state = arch_interrupt_save();
    ASSERT_EQ(true, arch_ints_disabled());
    interrupt_saved_state_t state2 = arch_interrupt_save();
    ASSERT_EQ(true, arch_ints_disabled());
    arch_interrupt_restore(state2);
    ASSERT_EQ(true, arch_ints_disabled());
    arch_interrupt_restore(state);
    ASSERT_EQ(false, arch_ints_disabled());
  }

  END_TEST;
}

static bool interrupt_save_restore_guard_test() {
  BEGIN_TEST;

  // validate that a save/restore C++ guard works
  ASSERT_EQ(false, arch_ints_disabled());
  {
    InterruptDisableGuard irqd;
    ASSERT_EQ(true, arch_ints_disabled());
  }
  ASSERT_EQ(false, arch_ints_disabled());

  // validate that a nested guard works
  {
    InterruptDisableGuard irqd;
    ASSERT_EQ(true, arch_ints_disabled());
    {
      InterruptDisableGuard irqd2;
      ASSERT_EQ(true, arch_ints_disabled());
    }
    ASSERT_EQ(true, arch_ints_disabled());
  }
  ASSERT_EQ(false, arch_ints_disabled());

  // validate that reenable works
  {
    InterruptDisableGuard irqd;
    ASSERT_EQ(true, arch_ints_disabled());
    irqd.Reenable();
    ASSERT_EQ(false, arch_ints_disabled());
    irqd.Reenable();
    ASSERT_EQ(false, arch_ints_disabled());
  }
  ASSERT_EQ(false, arch_ints_disabled());

  // validate that nested reenable works
  {
    InterruptDisableGuard irqd;
    ASSERT_EQ(true, arch_ints_disabled());
    {
      InterruptDisableGuard irqd2;
      ASSERT_EQ(true, arch_ints_disabled());
      irqd2.Reenable();
      ASSERT_EQ(true, arch_ints_disabled());
      irqd2.Reenable();
      ASSERT_EQ(true, arch_ints_disabled());
    }
    ASSERT_EQ(true, arch_ints_disabled());
  }
  ASSERT_EQ(false, arch_ints_disabled());

  END_TEST;
}

UNITTEST_START_TESTCASE(interrupt_disable_tests)
UNITTEST("interrupt_disable_test", interrupt_disable_test)
UNITTEST("interrupt_save_restore_test", interrupt_save_restore_test)
UNITTEST("interrupt_save_restore_guard_test", interrupt_save_restore_guard_test)
UNITTEST_END_TESTCASE(interrupt_disable_tests, "interrupt_tests", "Test arch enable/disable interrupt routines.")
