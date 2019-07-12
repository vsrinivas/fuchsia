// Copyright 2016, 2018 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <debug.h>
#include <kernel/atomic.h>
#include <kernel/cpu.h>
#include <kernel/mp.h>
#include <kernel/mutex.h>
#include <kernel/thread.h>
#include <ktl/popcount.h>
#include <lib/unittest/unittest.h>
#include <zircon/errors.h>
#include <zircon/types.h>

namespace {

// Wait for condition |cond| to become true, polling with slow exponential backoff
// to avoid pegging the CPU.
template <typename F>
void wait_for_cond(F cond) {
  zx_duration_t wait_duration = ZX_USEC(1);
  while (!cond()) {
    thread_sleep_relative(wait_duration);
    // Increase wait_duration time by ~10%.
    wait_duration += (wait_duration / 10u + 1u);
  }
}

struct YieldData {
  volatile int* done;
  volatile int* started;
};

// This thread will immediately yield, resulting in not fully using a given
// quantum.
static int yielding_tester(void* arg) {
  volatile int* done = reinterpret_cast<YieldData*>(arg)->done;
  volatile int* started = reinterpret_cast<YieldData*>(arg)->started;
  atomic_add(started, 1);
  for (;;) {
    thread_yield();
    arch_spinloop_pause();
    if (atomic_load(done) == 2)
      break;
  }
  return 0;
}

static int end_yielders_tester(void* arg) {
  volatile int* done = reinterpret_cast<YieldData*>(arg)->done;
  volatile int* started = reinterpret_cast<YieldData*>(arg)->started;
  atomic_add(started, 1);
  for (;;) {
    if (atomic_load(done) == 1) {
      atomic_add(done, 1);
      break;
    }
    arch_spinloop_pause();
  }
  return 0;
}

// In https://crbug.com/959245 and ZX-4410 a bunch of userspace yield-spinlocks
// caused a test hang, when there was num_cpus of them, and the yield deboost
// (for not expiring the quantum) ended up keeping them at higher priority than
// thread doing actual work.
static bool yield_deboost_test() {
  BEGIN_TEST;

  volatile int done = 0;
  volatile int started = 0;
  YieldData data = {&done, &started};

  constexpr int kNumYieldThreads = 128;
  constexpr int kNumTotalThreads = kNumYieldThreads + 1;
  thread_t* threads[kNumTotalThreads];

  // Start a pile of threads that all spin-yield.
  for (int i = 0; i < kNumYieldThreads; ++i) {
    threads[i] = thread_create("yielder", &yielding_tester, reinterpret_cast<void*>(&data),
                               DEFAULT_PRIORITY);
    ASSERT_NONNULL(threads[i], "thread_create");
    thread_resume(threads[i]);
  }

  // Start the potentially-starved thread.
  int starve = kNumYieldThreads;
  threads[starve] = thread_create("ender", &end_yielders_tester, reinterpret_cast<void*>(&data),
                                  DEFAULT_PRIORITY);
  ASSERT_NONNULL(threads[starve], "thread_create");
  thread_resume(threads[starve]);

  while (atomic_load(&started) < kNumTotalThreads) {
    // Wait until all the threads have started.
  }

  // This thread gets a positive boost when waking from sleep, so it should be
  // able to set done to 1. If the yield bug isn't happening, the non-yielding
  // thread will in turn set it to 2, which tells the yielders to exit. When
  // yield()ing is keeping the yielding threads at a higher priority than the
  // end_yielders, done will never move to 2, and so the test will hang when
  // trying to join the yield threads below.
  thread_sleep_relative(ZX_MSEC(100));
  atomic_add(&done, 1);

  TRACEF("going to join %d threads\n", kNumTotalThreads);
  for (int i = 0; i < kNumTotalThreads; ++i) {
    thread_join(threads[i], NULL, ZX_TIME_INFINITE);
  }

  END_TEST;
}

bool set_affinity_self_test() {
  BEGIN_TEST;

  // Our worker thread will attempt to schedule itself onto each core, one at
  // a time, and ensure it landed in the right location.
  cpu_mask_t online_cpus = mp_get_online_mask();
  ASSERT_NE(online_cpus, 0u, "Expected at least one CPU to be online.");
  auto worker_body = +[](void* arg) -> int {
    cpu_mask_t& online_cpus = *reinterpret_cast<cpu_mask_t*>(arg);
    thread_t* const self = get_current_thread();

    for (cpu_num_t c = 0u; c <= highest_cpu_set(online_cpus); c++) {
      // Skip offline CPUs.
      if ((cpu_num_to_mask(c) & online_cpus) == 0) {
        continue;
      }

      // Set affinity to the given core.
      thread_set_cpu_affinity(self, cpu_num_to_mask(c));

      // Ensure we are on the correct CPU.
      const cpu_num_t current_cpu = arch_curr_cpu_num();
      if (current_cpu != c) {
        UNITTEST_FAIL_TRACEF("Expected to be running on CPU %u, but actually running on %u.", c,
                             current_cpu);
        return ZX_ERR_INTERNAL;
      }
    }

    return ZX_OK;
  };
  thread_t* worker =
      thread_create("set_affinity_self_test_worker", worker_body, &online_cpus, DEFAULT_PRIORITY);
  ASSERT_NONNULL(worker, "thread_create failed.");
  thread_resume(worker);

  // Wait for the worker thread to test itself.
  int worker_retcode;
  ASSERT_EQ(thread_join(worker, &worker_retcode, ZX_TIME_INFINITE), ZX_OK,
            "Failed to join thread.");
  EXPECT_EQ(worker_retcode, ZX_OK, "Worker thread failed.");

  END_TEST;
}

bool set_affinity_other_test() {
  BEGIN_TEST;

  struct WorkerState {
    volatile int current_cpu = -1;
    volatile int should_stop = 0;
  } state;

  // Start a worker, which reports the CPU it is running on.
  auto worker_body = [](void* arg) -> int {
    WorkerState& state = *reinterpret_cast<WorkerState*>(arg);
    while (atomic_load(&state.should_stop) != 1) {
      atomic_store(&state.current_cpu, arch_curr_cpu_num());
    }
    return 0;
  };
  thread_t* worker =
      thread_create("set_affinity_other_test_worker", worker_body, &state, LOW_PRIORITY);
  thread_resume(worker);

  // Migrate the worker task amongst different threads.
  const cpu_mask_t online_cpus = mp_get_online_mask();
  ASSERT_NE(online_cpus, 0u, "Expected at least one CPU to be online.");
  for (cpu_num_t c = 0u; c <= highest_cpu_set(online_cpus); c++) {
    // Skip offline CPUs.
    if ((cpu_num_to_mask(c) & online_cpus) == 0) {
      continue;
    }

    // Set affinity to the given core.
    thread_set_cpu_affinity(worker, cpu_num_to_mask(c));

    // Wait for it to land on the correct CPU.
    wait_for_cond(
        [c, &state]() { return static_cast<cpu_num_t>(atomic_load(&state.current_cpu)) == c; });
  }

  // Done.
  atomic_store(&state.should_stop, 1);
  int worker_retcode;
  ASSERT_EQ(thread_join(worker, &worker_retcode, ZX_TIME_INFINITE), ZX_OK,
            "Failed to join thread.");

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(thread_tests)
UNITTEST("yield_deboost_test", yield_deboost_test)
UNITTEST("set_affinity_self_test", set_affinity_self_test)
UNITTEST("set_affinity_other_test", set_affinity_other_test)
UNITTEST_END_TESTCASE(thread_tests, "thread", "thread tests");
