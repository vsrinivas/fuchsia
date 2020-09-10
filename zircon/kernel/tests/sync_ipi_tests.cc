// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2012 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <err.h>
#include <lib/unittest/unittest.h>
#include <stdio.h>
#include <trace.h>
#include <zircon/types.h>

#include <arch/ops.h>
#include <fbl/algorithm.h>
#include <kernel/cpu.h>
#include <kernel/event.h>
#include <kernel/mp.h>
#include <kernel/thread.h>
#include <ktl/iterator.h>

#include "tests.h"

#define LOCAL_TRACE 0

#define TEST_RUNS 1000

static void inorder_count_task(void* raw_context) {
  ASSERT(arch_ints_disabled());
  ASSERT(arch_blocking_disallowed());
  ktl::atomic<int>* inorder_counter = reinterpret_cast<ktl::atomic<int>*>(raw_context);
  cpu_num_t cpu_num = arch_curr_cpu_num();

  int oldval = inorder_counter->fetch_add(1);
  ASSERT(oldval == (int)cpu_num);
  LTRACEF("  CPU %u checked in\n", cpu_num);
}

static void counter_task(void* raw_context) {
  ASSERT(arch_ints_disabled());
  ASSERT(arch_blocking_disallowed());
  ktl::atomic<int>* counter = reinterpret_cast<ktl::atomic<int>*>(raw_context);
  (*counter)++;
}

static int deadlock_test_thread(void* arg) {
  Event* gate = (Event*)arg;
  gate->Wait();

  ktl::atomic<int> counter(0);
  interrupt_saved_state_t int_state = arch_interrupt_save();
  mp_sync_exec(MP_IPI_TARGET_ALL_BUT_LOCAL, 0, counter_task, &counter);
  arch_interrupt_restore(int_state);
  return 0;
}

static void deadlock_test(void) {
  /* Test for a deadlock caused by multiple CPUs broadcasting concurrently */

  Event gate;

  Thread* threads[5] = {0};
  for (auto& thread : threads) {
    thread = Thread::Create("sync_ipi_deadlock", deadlock_test_thread, &gate, DEFAULT_PRIORITY);
    if (!thread) {
      TRACEF("  failed to create thread\n");
      goto cleanup;
    }
    thread->Resume();
  }

  gate.Signal();

cleanup:
  for (Thread* thread : threads) {
    if (thread) {
      thread->Join(NULL, ZX_TIME_INFINITE);
    }
  }
}

static bool sync_ipi_tests() {
  BEGIN_TEST;

  uint num_cpus = arch_max_num_cpus();
  uint online = mp_get_online_mask();
  if (online != (1U << num_cpus) - 1) {
    printf("Can only run test with all CPUs online\n");
    return true;
  }

  uint runs = TEST_RUNS;

  /* Test that we're actually blocking and only signaling the ones we target */
  for (uint i = 0; i < runs; ++i) {
    LTRACEF("Sequential test\n");
    ktl::atomic<int> inorder_counter = 0;
    for (cpu_num_t i = 0; i < num_cpus; ++i) {
      mp_sync_exec(MP_IPI_TARGET_MASK, 1u << i, inorder_count_task, &inorder_counter);
      LTRACEF("  Finished signaling CPU %u\n", i);
    }
  }

  /* Test that we can signal multiple CPUs at the same time */
  for (uint i = 0; i < runs; ++i) {
    LTRACEF("Counter test (%u CPUs)\n", num_cpus);
    ktl::atomic<int> counter = 0;

    {
      InterruptDisableGuard irqd;

      mp_sync_exec(MP_IPI_TARGET_ALL_BUT_LOCAL, 0, counter_task, &counter);
    }

    LTRACEF("  Finished signaling all but local (%d)\n", counter.load());
    ASSERT((uint)counter.load() == num_cpus - 1);
  }

  for (uint i = 0; i < runs; ++i) {
    LTRACEF("Deadlock test\n");
    deadlock_test();
    LTRACEF("Deadlock test passed\n");
  }

  END_TEST;
}

UNITTEST_START_TESTCASE(sync_ipi_tests)
UNITTEST("sync_ipi_tests", sync_ipi_tests)
UNITTEST_END_TESTCASE(sync_ipi_tests, "sync_ipi_tests", "sync_ipi_tests")
