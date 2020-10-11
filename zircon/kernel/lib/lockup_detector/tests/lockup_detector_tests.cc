// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/lockup_detector.h>
#include <lib/unittest/unittest.h>

#include <fbl/auto_call.h>
#include <kernel/auto_preempt_disabler.h>
#include <kernel/percpu.h>

namespace {

bool NestedCriticalSectionTest() {
  BEGIN_TEST;

  AutoPreemptDisabler<APDInitialState::PREEMPT_DISABLED> ap_disabler;

  // For the context of this test, use the maximum threshold to prevent the detector from "firing".
  auto cleanup = fbl::MakeAutoCall(
      [orig = lockup_get_threshold_ticks()]() { lockup_set_threshold_ticks(orig); });
  lockup_set_threshold_ticks(INT64_MAX);

  LockupDetectorState* state = &get_local_percpu()->lockup_detector_state;
  EXPECT_EQ(0u, state->critical_section_depth);
  EXPECT_EQ(0u, state->begin_ticks);

  zx_ticks_t now = current_ticks();

  lockup_begin();
  EXPECT_EQ(1u, state->critical_section_depth);

  const zx_ticks_t begin_ticks = state->begin_ticks;
  EXPECT_GE(state->begin_ticks, now);

  lockup_begin();
  EXPECT_EQ(2u, state->critical_section_depth);

  // No change because only the outer most critical section is tracked.
  EXPECT_EQ(begin_ticks, state->begin_ticks);

  lockup_end();
  EXPECT_EQ(1u, state->critical_section_depth);

  EXPECT_EQ(begin_ticks, state->begin_ticks);

  lockup_end();
  EXPECT_EQ(0u, state->critical_section_depth);
  EXPECT_EQ(0, state->begin_ticks);

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(lockup_detetcor_tests)
UNITTEST("nested_critical_section", NestedCriticalSectionTest)
UNITTEST_END_TESTCASE(lockup_detetcor_tests, "lockup_detector", "lockup_detector tests")
