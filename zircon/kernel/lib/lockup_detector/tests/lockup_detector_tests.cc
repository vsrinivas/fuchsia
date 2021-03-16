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

  AutoPreemptDisabler ap_disabler;

  // For the context of this test, use the maximum threshold to prevent the detector from "firing".
  auto cleanup = fbl::MakeAutoCall(
      [orig = lockup_get_cs_threshold_ticks()]() { lockup_set_cs_threshold_ticks(orig); });
  lockup_set_cs_threshold_ticks(INT64_MAX);

  const LockupDetectorState& state = get_local_percpu()->lockup_detector_state;
  const auto& cs_state = state.critical_section;

  EXPECT_EQ(0u, cs_state.depth);
  EXPECT_EQ(0u, cs_state.begin_ticks);

  static constexpr const char kOuter[] = "NestedCriticalSectionTest-outer";
  lockup_begin(kOuter);
  EXPECT_EQ(1u, cs_state.depth);
  EXPECT_EQ(0u, cs_state.begin_ticks);
  EXPECT_EQ(cs_state.name.load(), kOuter);

  static constexpr const char kInner[] = "NestedCriticalSectionTest-inner";
  lockup_begin(kInner);
  EXPECT_EQ(2u, cs_state.depth);
  // No change because only the outer most critical section is tracked.
  EXPECT_EQ(0u, cs_state.begin_ticks);
  EXPECT_EQ(cs_state.name.load(), kOuter);

  lockup_end();
  EXPECT_EQ(1u, cs_state.depth);
  EXPECT_EQ(0u, cs_state.begin_ticks);
  EXPECT_EQ(cs_state.name.load(), kOuter);

  lockup_end();
  EXPECT_EQ(0u, cs_state.depth);
  EXPECT_EQ(0, cs_state.begin_ticks);
  EXPECT_EQ(cs_state.name.load(), nullptr);

  END_TEST;
}

bool NestedTimedCriticalSectionTest() {
  BEGIN_TEST;

  AutoPreemptDisabler ap_disabler;

  // For the context of this test, use the maximum threshold to prevent the detector from "firing".
  auto cleanup = fbl::MakeAutoCall(
      [orig = lockup_get_cs_threshold_ticks()]() { lockup_set_cs_threshold_ticks(orig); });
  lockup_set_cs_threshold_ticks(INT64_MAX);

  const LockupDetectorState& state = get_local_percpu()->lockup_detector_state;
  const auto& cs_state = state.critical_section;

  EXPECT_EQ(0u, cs_state.depth);
  EXPECT_EQ(0u, cs_state.begin_ticks);

  zx_ticks_t now = current_ticks();

  static constexpr const char kOuter[] = "NestedTimedCriticalSectionTest-outer";
  lockup_timed_begin(kOuter);
  EXPECT_EQ(1u, cs_state.depth);

  const zx_ticks_t begin_ticks = cs_state.begin_ticks;
  EXPECT_GE(cs_state.begin_ticks, now);
  EXPECT_EQ(cs_state.name.load(), kOuter);

  static constexpr const char kInner[] = "NestedTimedCriticalSectionTest-inner";
  lockup_timed_begin(kInner);
  EXPECT_EQ(2u, cs_state.depth);

  // No change because only the outer most critical section is tracked.
  EXPECT_EQ(begin_ticks, cs_state.begin_ticks);
  EXPECT_EQ(cs_state.name.load(), kOuter);

  lockup_timed_end();
  EXPECT_EQ(1u, cs_state.depth);

  EXPECT_EQ(begin_ticks, cs_state.begin_ticks);
  EXPECT_EQ(cs_state.name.load(), kOuter);

  lockup_timed_end();
  EXPECT_EQ(0u, cs_state.depth);
  EXPECT_EQ(0, cs_state.begin_ticks);
  EXPECT_EQ(cs_state.name.load(), nullptr);

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(lockup_detetcor_tests)
UNITTEST("nested_critical_section", NestedCriticalSectionTest)
UNITTEST("nested_timed_critical_section", NestedTimedCriticalSectionTest)
UNITTEST_END_TESTCASE(lockup_detetcor_tests, "lockup_detector", "lockup_detector tests")
