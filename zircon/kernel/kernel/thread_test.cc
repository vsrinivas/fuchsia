// Copyright 2016, 2018 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <debug.h>
#include <lib/unittest/unittest.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <kernel/atomic.h>
#include <kernel/cpu.h>
#include <kernel/event.h>
#include <kernel/mp.h>
#include <kernel/mutex.h>
#include <kernel/thread.h>
#include <ktl/popcount.h>
#include <ktl/unique_ptr.h>

#include "zircon/time.h"

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

// Create and manage a spinning thread.
class WorkerThread {
 public:
  explicit WorkerThread(const char* name) {
    thread_ = thread_create(name, &WorkerThread::WorkerBody, this, LOW_PRIORITY);
    ASSERT(thread_ != nullptr);
  }

  ~WorkerThread() { Join(); }

  void Start() { thread_resume(thread_); }

  void Join() {
    if (thread_ != nullptr) {
      atomic_store(&worker_should_stop_, 1);
      int unused_retcode;
      zx_status_t result = thread_join(thread_, &unused_retcode, ZX_TIME_INFINITE);
      ASSERT(result == ZX_OK);
      thread_ = nullptr;
    }
  }

  void WaitForWorkerProgress() {
    int start_iterations = worker_iterations();
    wait_for_cond([start_iterations, this]() { return start_iterations != worker_iterations(); });
  }

  thread_t* thread() const { return thread_; }

  int worker_iterations() { return atomic_load(&worker_iterations_); }

  DISALLOW_COPY_ASSIGN_AND_MOVE(WorkerThread);

 private:
  static int WorkerBody(void* arg) {
    auto* self = reinterpret_cast<WorkerThread*>(arg);
    while (atomic_load(&self->worker_should_stop_) == 0) {
      atomic_add(&self->worker_iterations_, 1);
    }
    return 0;
  }

  volatile int worker_iterations_ = 0;
  volatile int worker_should_stop_ = 0;
  thread_t* thread_;
};

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

bool thread_last_cpu_new_thread() {
  BEGIN_TEST;

  // Create a worker, but don't start it.
  WorkerThread worker("last_cpu_new_thread");

  // Ensure we get INVALID_CPU as last cpu.
  ASSERT_EQ(thread_last_cpu(worker.thread()), INVALID_CPU, "Last CPU on unstarted thread invalid.");

  // Clean up the thread.
  worker.Start();
  worker.Join();

  END_TEST;
}

bool thread_last_cpu_running_thread() {
  BEGIN_TEST;

  WorkerThread worker("last_cpu_running_thread");
  worker.Start();

  // Migrate the worker task across different CPUs.
  const cpu_mask_t online_cpus = mp_get_online_mask();
  ASSERT_NE(online_cpus, 0u, "Expected at least one CPU to be online.");
  for (cpu_num_t c = 0u; c <= highest_cpu_set(online_cpus); c++) {
    // Skip offline CPUs.
    if ((cpu_num_to_mask(c) & online_cpus) == 0) {
      continue;
    }

    // Set affinity to the given core.
    thread_set_cpu_affinity(worker.thread(), cpu_num_to_mask(c));

    // Ensure it is reported at the correct CPU.
    wait_for_cond([c, &worker]() { return thread_last_cpu(worker.thread()) == c; });
  }

  END_TEST;
}

bool thread_empty_soft_affinity_mask() {
  BEGIN_TEST;

  WorkerThread worker("empty_soft_affinity_mask");
  worker.Start();

  // Wait for the thread to start running.
  worker.WaitForWorkerProgress();

  // Set affinity to an invalid (empty) mask.
  thread_set_soft_cpu_affinity(worker.thread(), 0);

  // Ensure that the thread is still running.
  worker.WaitForWorkerProgress();

  END_TEST;
}

bool thread_conflicting_soft_and_hard_affinity() {
  BEGIN_TEST;

  // Find two different CPUs to run our tests on.
  cpu_mask_t online = mp_get_online_mask();
  ASSERT_TRUE(online != 0, "No CPUs online.");
  cpu_num_t a = highest_cpu_set(online);
  cpu_num_t b = lowest_cpu_set(online);
  if (a == b) {
    // Skip test on single CPU machines.
    unittest_printf("Only 1 CPU active in this machine. Skipping test.\n");
    return true;
  }

  WorkerThread worker("conflicting_soft_and_hard_affinity");
  worker.Start();

  // Set soft affinity to CPU A, wait for the thread to start running there.
  thread_set_soft_cpu_affinity(worker.thread(), cpu_num_to_mask(a));
  wait_for_cond([&worker, a]() { return thread_last_cpu(worker.thread()) == a; });

  // Set hard affinity to CPU B, ensure the thread migrates there.
  thread_set_cpu_affinity(worker.thread(), cpu_num_to_mask(b));
  wait_for_cond([&worker, b]() { return thread_last_cpu(worker.thread()) == b; });

  // Remove the hard affinity. Make sure the thread migrates back to CPU A.
  thread_set_cpu_affinity(worker.thread(), CPU_MASK_ALL);
  wait_for_cond([&worker, a]() { return thread_last_cpu(worker.thread()) == a; });

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(thread_tests)
UNITTEST("set_affinity_self_test", set_affinity_self_test)
UNITTEST("set_affinity_other_test", set_affinity_other_test)
UNITTEST("thread_last_cpu_new_thread", thread_last_cpu_new_thread)
UNITTEST("thread_last_cpu_running_thread", thread_last_cpu_running_thread)
UNITTEST("thread_empty_soft_affinity_mask", thread_empty_soft_affinity_mask)
UNITTEST("thread_conflicting_soft_and_hard_affinity", thread_conflicting_soft_and_hard_affinity)
UNITTEST_END_TESTCASE(thread_tests, "thread", "thread tests")
