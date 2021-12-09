// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/concurrent/seqlock.h>

#include <chrono>
#include <thread>

#include <zxtest/zxtest.h>

namespace test {

using concurrent::SeqLock;

TEST(SeqLock, UncontestedRead) {
  SeqLock lock;

  // With no writer, read transactions should always succeed.
  SeqLock::ReadTransactionToken token1 = lock.BeginReadTransaction();
  ASSERT_TRUE(lock.EndReadTransaction(token1));

  // A second transaction with no write in-between should also succeed, and the
  // reported sequence number should be unchanged.
  SeqLock::ReadTransactionToken token2 = lock.BeginReadTransaction();
  ASSERT_TRUE(lock.EndReadTransaction(token2));
  ASSERT_EQ(token1.seq_num(), token2.seq_num());

  // After a write cycle, further subsequent read transactions should also
  // succeed, but with a different sequence number.
  lock.Acquire();
  lock.Release();
  SeqLock::ReadTransactionToken token3 = lock.BeginReadTransaction();
  ASSERT_TRUE(lock.EndReadTransaction(token3));
  ASSERT_NE(token1.seq_num(), token3.seq_num());
}

TEST(SeqLock, ContestedRead) {
  SeqLock lock;

  // Any write cycle which happens during a read should cause the read
  // transaction to fail.
  SeqLock::ReadTransactionToken token = lock.BeginReadTransaction();

  // Note that to keep life simple, and single threaded, we need to disable
  // clang's static analysis while we go through the write cycle.  While this
  // would not actually deadlock in any way, to clang, it looks like we are
  // attempting to obtain a capability exclusively while we already have
  // acquired that capability for shared access.
  [&]() __TA_NO_THREAD_SAFETY_ANALYSIS {
    lock.Acquire();
    lock.Release();
  }();

  ASSERT_FALSE(lock.EndReadTransaction(token));
}

TEST(SeqLock, ReadTimeouts) {
  SeqLock lock;

  // Trying to begin a read transaction when there is no write-cycle in flight
  // should always succeed, even with a timeout of zero.
  SeqLock::ReadTransactionToken token;
  if (lock.TryBeginReadTransaction(token, 0)) {
    ASSERT_TRUE(lock.EndReadTransaction(token));
  } else {
    ASSERT_TRUE(false);
  }

  // Same is true for deadlines in the past.
  if (lock.TryBeginReadTransactionDeadline(token, 0)) {
    ASSERT_TRUE(lock.EndReadTransaction(token));
  } else {
    ASSERT_TRUE(false);
  }

  // Attempting to start a transaction while a write cycle is in progress should
  // always timeout.
  [&]() __TA_NO_THREAD_SAFETY_ANALYSIS { lock.Acquire(); }();

  if (lock.TryBeginReadTransaction(token, ZX_MSEC(100))) {
    ASSERT_TRUE(lock.EndReadTransaction(token));
    ASSERT_TRUE(false);
  }

  // Unfortunately, we do not have a great way of checking the deadline variant
  // of this assertion, since the OS abstraction being using by the SeqLock
  // implementation is not currently guaranteed to be visible to us.  If we know
  // for sure that we are running on fuchsia, however, we should be able to
  // assume zx_clock_get_monotonic is our proper time reference.  Otherwise, we
  // use a deadline which is almost certainly in the past.  The operation should
  // still fail, it just will not end up spinning at all.
  zx_time_t now{0};
#if defined(__Fuchsia__)
  now = zx_clock_get_monotonic();
#endif
  if (lock.TryBeginReadTransactionDeadline(token, now + ZX_MSEC(100))) {
    ASSERT_TRUE(lock.EndReadTransaction(token));
    ASSERT_TRUE(false);
  }
}

TEST(SeqLock, UncontestedWrite) {
  SeqLock lock;

  // This one seems pretty trivial.  As long as there is only one writer,
  // acquire operations should always immediately succeed (including the
  // timeout/deadline versions, even if their timeout/deadlines are 0 or in the
  // past.
  constexpr uint32_t kTrials = 1000;
  for (uint32_t i = 0; i < kTrials; ++i) {
    lock.Acquire();
    lock.Release();

    if (lock.TryAcquire(0)) {
      lock.Release();
    } else {
      ASSERT_TRUE(false);
    }

    if (lock.TryAcquireDeadline(0)) {
      lock.Release();
    } else {
      ASSERT_TRUE(false);
    }
  }
}

TEST(SeqLock, ContestedWrite) {
  SeqLock lock;

  // Simulate contention, then make sure that all of the timeout forms of
  // Acquire time out.
  [&]() __TA_NO_THREAD_SAFETY_ANALYSIS { lock.Acquire(); }();

  if (lock.TryAcquire(ZX_MSEC(100))) {
    lock.Release();
    ASSERT_TRUE(false);
  }

  zx_time_t now{0};
#if defined(__Fuchsia__)
  now = zx_clock_get_monotonic();
#endif
  if (lock.TryAcquireDeadline(now + ZX_MSEC(100))) {
    lock.Release();
    ASSERT_TRUE(false);
  }

  // Make a best-effort attempt to validate a normal Acquire.
  //
  // Note that this can never be a conclusive test.  In addition to never being
  // able to absolutely guarantee that our test thread has actually started the
  // acquire operation after signaling to us that it has (via the shared bool),
  // no matter how long we wait, we can never actually prove that it the test
  // thread _wouldn't_ have eventually entered the exclusive portion of the lock
  // had we simply waited a bit longer.
  enum class State : uint32_t {
    NotStarted,
    AttemptingAcquire,
    AcquireSucceeded,
  };
  static_assert(std::atomic<State>::is_always_lock_free);

  std::atomic<State> state{State::NotStarted};
  std::thread acquire_thread([&]() {
    state.store(State::AttemptingAcquire);
    lock.Acquire();
    state.store(State::AcquireSucceeded);
    lock.Release();
  });

  // Wait forever for the thread start it's acquire attempt.
  while (state.load() != State::AttemptingAcquire) {
    // empty body.  Just spinning.
  }

  // Wait just a bit, then verify that the test thread has still not acquired
  // the lock.
  std::chrono::duration<int64_t, std::milli> delay{500};
  std::this_thread::sleep_for(delay);
  EXPECT_EQ(State::AttemptingAcquire, state.load());

  // Release the lock and verify that the test thread successfully acquires and
  // release it.
  [&]() __TA_NO_THREAD_SAFETY_ANALYSIS { lock.Release(); }();
  while (state.load() != State::AcquireSucceeded) {
    // empty body.  Just spinning.
  }

  // We should now be able to bounce through the lock without any significant
  // delay. The test thread may still be in the process of releasing the lock,
  // but it should eventually succeed.
  lock.Acquire();
  lock.Release();

  // The acquire_thread may not have exited yet, but it should do so in short
  // order.
  acquire_thread.join();
}

}  // namespace test
