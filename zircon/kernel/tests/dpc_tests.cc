// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>

#include <arch/arch_ops.h>
#include <arch/mp.h>
#include <fbl/alloc_checker.h>
#include <kernel/dpc.h>
#include <kernel/event.h>
#include <ktl/array.h>
#include <ktl/atomic.h>
#include <ktl/unique_ptr.h>

#include "tests.h"

struct event_signal_from_dpc_context {
  dpc_t dpc;
  event_t event;
  ktl::atomic<uint> expected_cpu;
  ktl::atomic<bool> dpc_started;
};
static void event_signal_from_dpc_check_cpu(dpc_t* dpc) {
  auto* const context = reinterpret_cast<struct event_signal_from_dpc_context*>(dpc->arg);
  context->dpc_started = true;

  // DPCs allow interrupts and blocking.
  DEBUG_ASSERT(!arch_ints_disabled());
  DEBUG_ASSERT(!arch_blocking_disallowed());
  DEBUG_ASSERT(context->expected_cpu == arch_curr_cpu_num());

  event_signal(&context->event, /*reschedule=*/false);
}

static bool test_dpc_queue() {
  BEGIN_TEST;

  static constexpr int kNumDPCs = 72;

  fbl::AllocChecker ac;
  auto context = ktl::make_unique<ktl::array<event_signal_from_dpc_context, kNumDPCs>>(&ac);
  ASSERT_TRUE(ac.check());

  // Init all the DPCs and supporting context.
  for (int i = 0; i < kNumDPCs; i++) {
    (*context)[i].dpc = DPC_INITIAL_VALUE;
    event_init(&(*context)[i].event, /*initial=*/false, /*flags=*/0);
    (*context)[i].dpc_started = false;
  }

  // Fire off DPCs.
  for (int i = 0; i < kNumDPCs; i++) {
    (*context)[i].dpc.func = event_signal_from_dpc_check_cpu;
    (*context)[i].dpc.arg = reinterpret_cast<void*>(&(*context)[i]);
    arch_disable_ints();
    (*context)[i].expected_cpu = arch_curr_cpu_num();
    dpc_queue(&(*context)[i].dpc, /*reschedule=*/false);
    arch_enable_ints();
  }
  for (int i = 0; i < kNumDPCs; i++) {
    if ((*context)[i].dpc_started) {
      // Once the DPC has started executing, we can reclaim the submitted dpc_t. Zero it to
      // try to check this.
      (*context)[i].dpc = DPC_INITIAL_VALUE;
    }
  }
  for (int i = 0; i < kNumDPCs; i++) {
    event_wait(&(*context)[i].event);
  }
  for (int i = 0; i < kNumDPCs; i++) {
    event_destroy(&(*context)[i].event);
  }

  END_TEST;
}

UNITTEST_START_TESTCASE(dpc_tests)
UNITTEST("basic test of dpc_queue", test_dpc_queue)
UNITTEST_END_TESTCASE(dpc_tests, "dpc_tests", "Tests of DPCs")
