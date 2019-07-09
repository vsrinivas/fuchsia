// Copyright 2016, 2018 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <debug.h>
#include <kernel/mutex.h>
#include <kernel/thread.h>
#include <lib/unittest/unittest.h>
#include <zircon/types.h>

namespace {

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

}  // namespace

UNITTEST_START_TESTCASE(thread_tests)
UNITTEST("yield_deboost_test", yield_deboost_test)
UNITTEST_END_TESTCASE(thread_tests, "thread", "thread tests");
