// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/synchronization/cond_var.h"

#include <stdint.h>
#include <stdlib.h>

#include <thread>
#include <type_traits>
#include <vector>

#include "gtest/gtest.h"
#include "lib/fxl/arraysize.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/synchronization/mutex.h"
#include "lib/fxl/test/timeout_tolerance.h"
#include "lib/fxl/time/stopwatch.h"

namespace fxl {
namespace {

constexpr TimeDelta kEpsilonTimeout = TimeDelta::FromMilliseconds(20);
constexpr TimeDelta kTinyTimeout = TimeDelta::FromMilliseconds(100);

// Sleeps for a "very small" amount of time.
void EpsilonRandomSleep() {
  std::chrono::milliseconds duration(static_cast<unsigned>(rand()) % 20u);
  std::this_thread::sleep_for(duration);
}

TEST(CondVarTest, Basic) {
  // Create/destroy.
  { CondVar cv; }

  // Signal with no waiter.
  {
    CondVar cv;
    cv.Signal();
    cv.SignalAll();
  }

  // Wait with a zero and with very short timeout.
  {
    Mutex mu;
    CondVar cv;

    MutexLocker locker(&mu);

    // Note: Theoretically, pthreads is allowed to wake us up spuriously, in
    // which case |WaitWithTimeout()| would return false. (This would also
    // happen if we're interrupted, e.g., by ^Z.)
    EXPECT_TRUE(cv.WaitWithTimeout(&mu, TimeDelta::Zero()));
    mu.AssertHeld();
    EXPECT_TRUE(cv.WaitWithTimeout(&mu, TimeDelta::FromMicroseconds(1)));
    mu.AssertHeld();
  }

  // Wait using |Wait()| or |WaitWithTimeout()|, to be signaled by |Signal()| or
  // |SignalAll()|.
  for (size_t i = 0; i < 30; i++) {
    Mutex mu;
    CondVar cv;
    bool condition = false;

    auto thread = std::thread([&mu, &cv, &condition]() {
      EpsilonRandomSleep();

      MutexLocker locker(&mu);
      condition = true;
      if (rand() % 2 == 0)
        cv.Signal();
      else
        cv.SignalAll();
    });

    EpsilonRandomSleep();

    MutexLocker locker(&mu);
    if (rand() % 2 == 0) {
      while (!condition) {
        cv.Wait(&mu);
        mu.AssertHeld();
      }
    } else {
      while (!condition) {
        cv.WaitWithTimeout(&mu, kTinyTimeout);
        mu.AssertHeld();
      }
    }

    thread.join();
  }
}

TEST(CondVarTest, SignalAll) {
  Mutex mu;
  CondVar cv;
  bool condition = false;

  for (size_t i = 0; i < 10; i++) {
    for (size_t num_waiters = 1; num_waiters < 5; num_waiters++) {
      std::vector<std::thread> threads;
      for (size_t j = 0; j < num_waiters; j++) {
        threads.push_back(std::thread([&mu, &cv, &condition]() {
          EpsilonRandomSleep();

          MutexLocker locker(&mu);
          if (rand() % 2 == 0) {
            while (!condition) {
              cv.Wait(&mu);
              mu.AssertHeld();
            }
          } else {
            while (!condition) {
              EXPECT_FALSE(cv.WaitWithTimeout(&mu, kTinyTimeout));
              mu.AssertHeld();
            }
          }
        }));
      }

      EpsilonRandomSleep();

      {
        MutexLocker locker(&mu);
        condition = true;
        cv.SignalAll();
      }

      for (auto& thread : threads)
        thread.join();
    }
  }
}

TEST(CondVarTest, Timeouts) {
  static const unsigned kTestTimeoutsMs[] = {0, 10, 20, 40, 80, 160};

  Stopwatch stopwatch;

  Mutex mu;
  CondVar cv;

  MutexLocker locker(&mu);

  for (size_t i = 0; i < arraysize(kTestTimeoutsMs); i++) {
    TimeDelta timeout = TimeDelta::FromMilliseconds(kTestTimeoutsMs[i]);

    stopwatch.Start();
    // See note in CondVarTest.Basic about spurious wakeups.
    EXPECT_TRUE(cv.WaitWithTimeout(&mu, timeout));
    TimeDelta elapsed = stopwatch.Elapsed();

    // It should time out after *at least* the specified amount of time.
    EXPECT_GE(elapsed, timeout - kTimeoutTolerance);
    // But we expect that it should time out soon after that amount of time.
    EXPECT_LT(elapsed, timeout + kEpsilonTimeout);
  }
}

// TODO(vtl): Test that |Signal()| (usually) wakes only one waiter.

}  // namespace
}  // namespace fxl
