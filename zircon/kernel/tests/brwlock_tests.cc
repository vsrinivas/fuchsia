// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>
#include <platform.h>

#include <fbl/algorithm.h>
#include <kernel/brwlock.h>
#include <kernel/mp.h>
#include <ktl/atomic.h>

#include "tests.h"

// Use a delay spinner to create fine grained delays between 0 and 1msec
static void rand_delay() {
  int64_t end = current_time() + (rand() % ZX_MSEC(1));
  do {
    thread_yield();
  } while (current_time() < end);
}

// Use a helper class for running tests so that worker threads and main thread all
// have easy access to shared state.
template <typename LockType>
class BrwLockTest {
 public:
  BrwLockTest() : state_(0), kill_(false) {}
  ~BrwLockTest() {}

  template <unsigned int readers, unsigned int writers, unsigned int upgraders>
  static bool RunTest() {
    BEGIN_TEST;

    BrwLockTest<LockType> test;
    thread_t* reader_threads[readers];
    thread_t* writer_threads[writers];
    thread_t* upgrader_threads[upgraders];

    int old_prio = get_current_thread()->base_priority;
    // Run at high priority so that we can be validating what the other threads are doing.
    // Unless we are a uniprocessor, in which case we will just have to live with poor
    // testing. If we do boost priority then we need to make sure worker threads
    // don't ever get scheduled on our core, since we will never block and so they
    // will starve. This is currently a known starvation problem with the scheduler.
    cpu_mask_t worker_mask = mp_get_online_mask();
    if (lowest_cpu_set(worker_mask) != highest_cpu_set(worker_mask)) {
      mp_get_online_mask();
      thread_set_priority(get_current_thread(), HIGH_PRIORITY);
      cpu_mask_t pin_mask = cpu_num_to_mask(lowest_cpu_set(worker_mask));
      thread_set_cpu_affinity(get_current_thread(), pin_mask);
      worker_mask -= pin_mask;
    } else {
      thread_set_priority(get_current_thread(), DEFAULT_PRIORITY);
    }

    // Start threads
    for (auto& t : reader_threads) {
      t = thread_create(
          "reader worker",
          [](void* arg) -> int {
            static_cast<BrwLockTest*>(arg)->ReaderWorker();
            return 0;
          },
          &test, DEFAULT_PRIORITY);
      thread_set_cpu_affinity(t, worker_mask);
      thread_resume(t);
    }
    for (auto& t : writer_threads) {
      t = thread_create(
          "writer worker",
          [](void* arg) -> int {
            static_cast<BrwLockTest*>(arg)->WriterWorker();
            return 0;
          },
          &test, DEFAULT_PRIORITY);
      thread_set_cpu_affinity(t, worker_mask);
      thread_resume(t);
    }
    for (auto& t : upgrader_threads) {
      t = thread_create(
          "upgrader worker",
          [](void* arg) -> int {
            static_cast<BrwLockTest*>(arg)->UpgraderWorker();
            return 0;
          },
          &test, DEFAULT_PRIORITY);
      thread_set_cpu_affinity(t, worker_mask);
      thread_resume(t);
    }

    zx_time_t start = current_time();
    zx_duration_t duration = ZX_MSEC(300);
    while (current_time() < start + duration) {
      uint32_t local_state = test.state_.load(ktl::memory_order_relaxed);
      uint32_t num_readers = local_state & 0xffff;
      uint32_t num_writers = local_state >> 16;
      EXPECT_LE(num_readers, readers + upgraders, "Too many readers");
      EXPECT_TRUE(num_writers == 0 || num_writers == 1, "Too many writers");
      EXPECT_TRUE((num_readers == 0 && num_writers == 0) || num_writers > 0 || num_readers > 0,
                  "Readers and writers");
      thread_yield();
    }

    // Shutdown all the threads. Validating they can shutdown is important
    // to ensure they didn't get stuck on the waitqueue and never woken up.
    test.kill_.store(true, ktl::memory_order_seq_cst);
    zx_time_t join_deadline = current_time() + ZX_SEC(5);
    for (auto& t : reader_threads) {
      zx_status_t status = thread_join(t, nullptr, join_deadline);
      EXPECT_EQ(status, ZX_OK, "Reader failed to complete");
    }
    for (auto& t : writer_threads) {
      zx_status_t status = thread_join(t, nullptr, join_deadline);
      EXPECT_EQ(status, ZX_OK, "Writer failed to complete");
    }
    for (auto& t : upgrader_threads) {
      zx_status_t status = thread_join(t, nullptr, join_deadline);
      EXPECT_EQ(status, ZX_OK, "Upgrader failed to complete");
    }
    EXPECT_EQ(test.state_.load(ktl::memory_order_seq_cst), 0u, "Threads still holding lock");

    // Restore original priority.
    thread_set_priority(get_current_thread(), old_prio);

    END_TEST;
  }

 private:
  void ReaderWorker() {
    while (!kill_.load(ktl::memory_order_relaxed)) {
      lock_.ReadAcquire();
      state_.fetch_add(1, ktl::memory_order_relaxed);
      thread_yield();
      state_.fetch_sub(1, ktl::memory_order_relaxed);
      lock_.ReadRelease();
      rand_delay();
    }
  }

  void WriterWorker() {
    while (!kill_.load(ktl::memory_order_relaxed)) {
      lock_.WriteAcquire();
      state_.fetch_add(0x10000, ktl::memory_order_relaxed);
      thread_yield();
      state_.fetch_sub(0x10000, ktl::memory_order_relaxed);
      lock_.WriteRelease();
      rand_delay();
    }
  }

  void UpgraderWorker() {
    while (!kill_.load(ktl::memory_order_relaxed)) {
      lock_.ReadAcquire();
      state_.fetch_add(1, ktl::memory_order_relaxed);
      thread_yield();
      state_.fetch_sub(1, ktl::memory_order_relaxed);
      lock_.ReadUpgrade();
      state_.fetch_add(0x10000, ktl::memory_order_relaxed);
      thread_yield();
      state_.fetch_sub(0x10000, ktl::memory_order_relaxed);
      lock_.WriteRelease();
      rand_delay();
    }
  }

  LockType lock_;
  ktl::atomic<uint32_t> state_;
  ktl::atomic<bool> kill_;
};

UNITTEST_START_TESTCASE(brwlock_tests)
// The number of threads to use for readers, writers and upgraders was chosen by manual
// instrumentation of the brwlock to see if all the different code paths were being hit.
UNITTEST("parallel readers(PI)", (BrwLockTest<BrwLockPi>::RunTest<8, 0, 0>))
UNITTEST("single writer(PI)", (BrwLockTest<BrwLockPi>::RunTest<0, 4, 0>))
UNITTEST("readers and writer(PI)", (BrwLockTest<BrwLockPi>::RunTest<4, 2, 0>))
UNITTEST("upgraders(PI)", (BrwLockTest<BrwLockPi>::RunTest<2, 0, 3>))
UNITTEST("parallel readers(No PI)", (BrwLockTest<BrwLockNoPi>::RunTest<8, 0, 0>))
UNITTEST("single writer(No PI)", (BrwLockTest<BrwLockNoPi>::RunTest<0, 4, 0>))
UNITTEST("readers and writer(No PI)", (BrwLockTest<BrwLockNoPi>::RunTest<4, 2, 0>))
UNITTEST("upgraders(No PI)", (BrwLockTest<BrwLockNoPi>::RunTest<2, 0, 3>))
UNITTEST_END_TESTCASE(brwlock_tests, "brwlock", "brwlock tests")
