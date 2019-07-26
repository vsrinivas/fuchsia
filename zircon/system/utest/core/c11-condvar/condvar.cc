// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <sched.h>
#include <threads.h>

#include <zxtest/zxtest.h>

namespace {

constexpr int kNumThreads = 3;
constexpr int* kIgnoreReturn = nullptr;

struct CondThreadArgs {
  mtx_t mutex;
  cnd_t cond;
  std::atomic<int> threads_started;
  std::atomic<int> threads_woken;
  std::atomic<bool> wait_condition;
};

TEST(ConditionalVariableTest, BroadcastSignalThreadWait) {
  auto CondWaitHelper = [](void* arg) -> int {
    CondThreadArgs* args = static_cast<CondThreadArgs*>(arg);

    mtx_lock(&args->mutex);
    args->threads_started++;
    while (args->wait_condition == false) {
      cnd_wait(&args->cond, &args->mutex);
    }
    args->threads_woken++;
    mtx_unlock(&args->mutex);

    return 0;
  };

  CondThreadArgs args = {};

  ASSERT_EQ(mtx_init(&args.mutex, mtx_plain), thrd_success);
  ASSERT_EQ(cnd_init(&args.cond), thrd_success);

  thrd_t threads[kNumThreads];
  for (auto& thread : threads) {
    ASSERT_EQ(thrd_create(&thread, CondWaitHelper, &args), thrd_success);
  }

  // Wait for all the threads to report that they've started, and
  // all have reached the wait.
  while (true) {
    mtx_lock(&args.mutex);
    int threads = args.threads_started;
    mtx_unlock(&args.mutex);
    if (threads == kNumThreads) {
      break;
    }
    sched_yield();
  }

  args.wait_condition = true;
  ASSERT_EQ(cnd_broadcast(&args.cond), thrd_success);

  // Wait for all the threads to report that they were woken.
  while (true) {
    int threads_woken = args.threads_woken;
    if (threads_woken == kNumThreads) {
      break;
    }
    sched_yield();
  }

  for (auto& thread : threads) {
    EXPECT_EQ(thrd_join(thread, kIgnoreReturn), thrd_success);
  }
}

TEST(ConditionalVariableTest, SignalThreadWait) {
  auto CondWaitHelper = [](void* arg) -> int {
    CondThreadArgs* args = static_cast<CondThreadArgs*>(arg);

    mtx_lock(&args->mutex);
    args->threads_started++;
    while (args->wait_condition == false) {
      cnd_wait(&args->cond, &args->mutex);
    }
    args->wait_condition = false;
    args->threads_woken++;
    mtx_unlock(&args->mutex);

    return 0;
  };

  CondThreadArgs args = {};

  ASSERT_EQ(mtx_init(&args.mutex, mtx_plain), thrd_success);
  ASSERT_EQ(cnd_init(&args.cond), thrd_success);

  thrd_t threads[kNumThreads];
  for (auto& thread : threads) {
    ASSERT_EQ(thrd_create(&thread, CondWaitHelper, &args), thrd_success);
  }

  // Wait for all the threads to report that they've started,
  // and all have reached the wait.
  while (true) {
    mtx_lock(&args.mutex);
    int threads = args.threads_started;
    mtx_unlock(&args.mutex);
    if (threads == kNumThreads) {
      break;
    }
    sched_yield();
  }

  for (int iteration = 0; iteration < kNumThreads; iteration++) {
    mtx_lock(&args.mutex);
    args.wait_condition = true;
    EXPECT_EQ(cnd_signal(&args.cond), thrd_success);
    mtx_unlock(&args.mutex);

    // Wait for one thread to report that it was woken.
    while (true) {
      mtx_lock(&args.mutex);
      int threads_woken = args.threads_woken;
      mtx_unlock(&args.mutex);
      if (threads_woken == iteration + 1) {
        break;
      }
      sched_yield();
    }
  }

  for (auto& thread : threads) {
    EXPECT_EQ(thrd_join(thread, kIgnoreReturn), thrd_success);
  }
}

void TimeAddNsec(struct timespec* ts, int nsec) {
  constexpr int kNsecPerSec = 1000000000;
  assert(nsec < kNsecPerSec);
  ts->tv_nsec += nsec;
  if (ts->tv_nsec > kNsecPerSec) {
    ts->tv_nsec -= kNsecPerSec;
    ts->tv_sec++;
  }
}

TEST(ConditionalVariableTest, ConditionalVariablesTimeout) {
  cnd_t cond = CND_INIT;
  mtx_t mutex = MTX_INIT;

  mtx_lock(&mutex);
  struct timespec delay;
  clock_gettime(CLOCK_REALTIME, &delay);
  TimeAddNsec(&delay, zx::msec(1).get());
  int result = cnd_timedwait(&cond, &mutex, &delay);
  mtx_unlock(&mutex);

  EXPECT_EQ(result, thrd_timedout, "Lock should have timedout");
}

}  // namespace
