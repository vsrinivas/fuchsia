// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include "arch/x86/idle_states.h"

#include <lib/unittest/unittest.h>
#include <platform.h>
#include <stdlib.h>
#include <zircon/time.h>

#include <arch/arch_ops.h>
#include <arch/x86.h>
#include <arch/x86/feature.h>
#include <kernel/thread.h>
#include <platform/timer.h>

namespace {

x86_idle_states_t kC1OnlyIdleStates = {
  .states = {X86_CSTATE_C1(0)},
  .default_state_mask = kX86IdleStateMaskC1Only,
};

x86_idle_states_t kKabyLakeIdleStates = {
    .states = {{.name = "C6", .mwait_hint = 0x50, .exit_latency = 1000, .flushes_tlb = true},
               {.name = "C3", .mwait_hint = 0x20, .exit_latency = 100, .flushes_tlb = true},
               {.name = "C1E", .mwait_hint = 0x01, .exit_latency = 10, .flushes_tlb = false},
               X86_CSTATE_C1(0)},
    .default_state_mask = 0b0000'0000'0011'1111,
};

bool test_c1_only() {
  BEGIN_TEST;

  X86IdleStates states(&kC1OnlyIdleStates);
  ASSERT_EQ(states.NumStates(), 1U);
  X86IdleState* state = states.PickIdleState();
  EXPECT_EQ(strcmp(state->Name(), "C1"), 0);
  EXPECT_EQ(state->MwaitHint(), 0x00u);

  END_TEST;
}

bool test_kbl() {
  BEGIN_TEST;

  X86IdleStates states(&kKabyLakeIdleStates);
  ASSERT_EQ(states.NumStates(), 4U);

  X86IdleState* state = states.PickIdleState();
  EXPECT_EQ(strcmp(state->Name(), "C1"), 0);
  EXPECT_EQ(state->MwaitHint(), 0x00u);

  states.RecordDuration(zx_duration_from_usec(3U));
  state = states.PickIdleState();
  EXPECT_EQ(strcmp(state->Name(), "C1"), 0);
  EXPECT_EQ(state->MwaitHint(), 0x00u);

  states.RecordDuration(zx_duration_from_usec(4U));
  state = states.PickIdleState();
  EXPECT_EQ(strcmp(state->Name(), "C1E"), 0);
  EXPECT_EQ(state->MwaitHint(), 0x01u);

  states.RecordDuration(zx_duration_from_usec(34U));
  state = states.PickIdleState();
  EXPECT_EQ(strcmp(state->Name(), "C3"), 0);
  EXPECT_EQ(state->MwaitHint(), 0x20u);

  states.RecordDuration(zx_duration_from_usec(334U));
  state = states.PickIdleState();
  EXPECT_EQ(strcmp(state->Name(), "C6"), 0);
  EXPECT_EQ(state->MwaitHint(), 0x50u);

  END_TEST;
}

bool test_kbl_statemask() {
  BEGIN_TEST;

  X86IdleStates states(&kKabyLakeIdleStates);

  // Empty mask will always choose C1 or C1E
  states.SetStateMask(0b0000'0000'0000'0000);
  X86IdleState* state = states.PickIdleState();
  EXPECT_EQ(state->MwaitHint(), 0x00u);
  EXPECT_EQ(strcmp(state->Name(), "C1"), 0);
  states.RecordDuration(zx_duration_from_usec(3U));
  state = states.PickIdleState();
  EXPECT_EQ(state->MwaitHint(), 0x00u);
  EXPECT_EQ(strcmp(state->Name(), "C1"), 0);
  states.RecordDuration(zx_duration_from_usec(4U));
  state = states.PickIdleState();
  EXPECT_EQ(state->MwaitHint(), 0x01u);
  EXPECT_EQ(strcmp(state->Name(), "C1E"), 0);
  states.RecordDuration(zx_duration_from_usec(34U));
  state = states.PickIdleState();
  EXPECT_EQ(state->MwaitHint(), 0x01u);
  EXPECT_EQ(strcmp(state->Name(), "C1E"), 0);
  states.RecordDuration(zx_duration_from_usec(334U));
  state = states.PickIdleState();
  EXPECT_EQ(state->MwaitHint(), 0x01u);
  EXPECT_EQ(strcmp(state->Name(), "C1E"), 0);

  // Mask to only allow C6, C1/C1E
  states.SetStateMask(0b0000'0000'0010'0001);
  states.RecordDuration(zx_duration_from_usec(0u));
  state = states.PickIdleState();
  EXPECT_EQ(state->MwaitHint(), 0x00u);
  EXPECT_EQ(strcmp(state->Name(), "C1"), 0);
  states.RecordDuration(zx_duration_from_usec(3U));
  state = states.PickIdleState();
  EXPECT_EQ(state->MwaitHint(), 0x00u);
  EXPECT_EQ(strcmp(state->Name(), "C1"), 0);
  states.RecordDuration(zx_duration_from_usec(4U));
  state = states.PickIdleState();
  EXPECT_EQ(state->MwaitHint(), 0x01u);
  EXPECT_EQ(strcmp(state->Name(), "C1E"), 0);
  states.RecordDuration(zx_duration_from_usec(34U));
  state = states.PickIdleState();
  EXPECT_EQ(state->MwaitHint(), 0x01u);
  EXPECT_EQ(strcmp(state->Name(), "C1E"), 0);
  states.RecordDuration(zx_duration_from_usec(334U));
  state = states.PickIdleState();
  EXPECT_EQ(state->MwaitHint(), 0x50u);
  EXPECT_EQ(strcmp(state->Name(), "C6"), 0);

  END_TEST;
}

volatile uint8_t monitor;

static uint8_t kGuardValue = UINT8_MAX;

static int poke_monitor(void* arg) {
  // A short sleep ensures the main test thread has time to set up the monitor
  // and enter MWAIT.
  thread_sleep_relative(zx_duration_from_msec(1));
  monitor = kGuardValue;
  return 0;
}

bool test_enter_idle_states() {
  BEGIN_TEST;

  monitor = 0;

  if (x86_feature_test(X86_FEATURE_MON)) {
    X86IdleStates states(x86_get_idle_states());
    for (uint8_t i = 0; i < states.NumStates(); ++i) {
      const X86IdleState& state = states.States()[i];

      unittest_printf("Entering state '%s' (MWAIT 0x%02x) on CPU %u\n", state.Name(),
                      state.MwaitHint(), arch_curr_cpu_num());

      // Thread must be created and started before arming the monitor,
      // since thread creation appears to trip the monitor latch prematurely.
      thread_t* thrd = thread_create("monitor_poker", &poke_monitor, nullptr, DEFAULT_PRIORITY);
      thread_resume(thrd);

      monitor = i;
      smp_mb();
      x86_monitor(&monitor);
      auto start = current_time();
      x86_mwait(state.MwaitHint());

      unittest_printf("Exiting state (%ld ns elapsed)\n", zx_time_sub_time(current_time(), start));
      thread_join(thrd, nullptr, ZX_TIME_INFINITE);
    }
  } else {
    unittest_printf("Skipping test; MWAIT/MONITOR not supported\n");
  }

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(x86_idle_states_tests)
UNITTEST("Select an idle state using data from a CPU with only C1.", test_c1_only)
UNITTEST("Select an idle state using data from a Kabylake CPU.", test_kbl)
UNITTEST("Select an idle state using data from a Kabylake CPU, respecting a mask of valid states.",
         test_kbl_statemask)
UNITTEST("Enter each supported idle state using MWAIT/MONITOR.", test_enter_idle_states)
UNITTEST_END_TESTCASE(x86_idle_states_tests, "x86_idle_states",
                      "Test idle state enumeration and selection (x86 only).")
