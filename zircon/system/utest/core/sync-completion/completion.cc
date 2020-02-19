// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sync/completion.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <sched.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>

#include <array>
#include <limits>

#include <zxtest/zxtest.h>

namespace {

struct TestThread {
 public:
  TestThread() = default;
  ~TestThread() { Join(true); }

  void StartAndBlock(const char* name, sync_completion_t* completion,
                     zx::time deadline = zx::time::infinite()) {
    ZX_ASSERT(completion_ == nullptr);
    ZX_ASSERT(completion != nullptr);

    // Remember our deadline, and make sure that our exited flag has been
    // cleared before starting the thread.
    deadline_ = deadline;
    exited_.store(false);

    auto thunk = [](void* ctx) -> int {
      auto thiz = reinterpret_cast<TestThread*>(ctx);
      return thiz->DoBlock();
    };

    completion_ = completion;
    auto result = thrd_create_with_name(&thread_, thunk, this, name);
    if (result != thrd_success) {
      completion_ = nullptr;
    }

    ASSERT_EQ(thrd_success, result);
  }

  void Join(bool force) {
    if (!started()) {
      return;
    }

    if (force) {
      ZX_ASSERT(completion_ != nullptr);
      sync_completion_signal(completion_);
    }

    int res = thrd_join(thread_, &thread_ret_);
    ZX_ASSERT(res == thrd_success);
    completion_ = nullptr;
  }

  zx_status_t IsBlockedOnFutex(bool* out_is_blocked) const {
    if (completion_ == nullptr) {
      return ZX_ERR_BAD_STATE;
    }

    zx_info_thread_t info;
    zx_status_t status = zx_object_get_info(thrd_get_zx_handle(thread_), ZX_INFO_THREAD, &info,
                                            sizeof(info), nullptr, nullptr);
    *out_is_blocked = (info.state == ZX_THREAD_STATE_BLOCKED_FUTEX);

    return status;
  }

  zx_status_t status() const { return status_.load(); }
  bool started() const { return (completion_ != nullptr); }
  bool exited() const { return exited_.load(); }

 private:
  int DoBlock() {
    status_.store(sync_completion_wait_deadline(completion_, deadline_.get()));
    exited_.store(true);
    return 0;
  }

  thrd_t thread_;
  int thread_ret_ = 0;
  zx::time deadline_{zx::time::infinite()};

  sync_completion_t* completion_ = nullptr;
  std::atomic<zx_status_t> status_;
  std::atomic<bool> exited_{true};
};

template <size_t N>
static void CheckAllBlockedOnFutex(const std::array<TestThread, N>& threads,
                                   bool* out_all_blocked) {
  *out_all_blocked = true;

  for (const auto& thread : threads) {
    zx_status_t res = thread.IsBlockedOnFutex(out_all_blocked);
    ASSERT_OK(res);

    if (!(*out_all_blocked)) {
      return;
    }
  }
}

template <size_t N>
static void WaitForAllBlockedOnFutex(const std::array<TestThread, N>& threads) {
  while (true) {
    bool done;

    CheckAllBlockedOnFutex(threads, &done);

    if (done) {
      return;
    }

    zx_nanosleep(zx_deadline_after(ZX_USEC(100)));
  }
}

constexpr size_t kMultiWaitThreadCount = 16;

TEST(SyncCompletionTest, Initializer) {
  // Let's not accidentally break .bss'd completions
  static sync_completion_t static_completion;
  sync_completion_t completion;
  int status = memcmp(&static_completion, &completion, sizeof(sync_completion_t));
  EXPECT_EQ(status, 0, "completion's initializer is not all zeroes");
}

template <size_t N>
void TestWait() {
  sync_completion_t completion;
  std::array<TestThread, N> threads;

  // Start the threads
  for (auto& thread : threads) {
    ASSERT_NO_FATAL_FAILURES(thread.StartAndBlock("TestWait", &completion));
  }

  // Wait until all of the threads have blocked, then signal the completion.
  ASSERT_NO_FATAL_FAILURES(WaitForAllBlockedOnFutex(threads));
  sync_completion_signal(&completion);

  // Wait for the threads to finish, and verify that they received the proper
  // wait result.
  for (auto& thread : threads) {
    thread.Join(false);
    ASSERT_OK(thread.status());
  }
}

TEST(SyncCompletionTest, SingleWait) { ASSERT_NO_FATAL_FAILURES(TestWait<1>()); }

TEST(SyncCompletionTest, MultiWait) {
  ASSERT_NO_FATAL_FAILURES(TestWait<kMultiWaitThreadCount>());
}

template <size_t N>
void TestWaitTimeout() {
  sync_completion_t completion;
  std::array<TestThread, N> threads;
  zx::time deadline = zx::clock::get_monotonic() + zx::msec(300);

  // Start the threads
  for (auto& thread : threads) {
    ASSERT_NO_FATAL_FAILURES(thread.StartAndBlock("TestWaitTimeout", &completion, deadline));
  }

  // Don't bother attempting to wait until threads have blocked; doing so will
  // just introduce a flake race.
  //
  // Do not signal the threads, just Wait for them to finish, and verify that
  // they received a TIMED_OUT error.
  for (auto& thread : threads) {
    thread.Join(false);
    ASSERT_STATUS(thread.status(), ZX_ERR_TIMED_OUT);
  }
}

TEST(SyncCompletionTest, TimeoutSingleWait) { ASSERT_NO_FATAL_FAILURES(TestWaitTimeout<1>()); }

TEST(SyncCompletionTest, TimeoutMultiWait) {
  ASSERT_NO_FATAL_FAILURES(TestWaitTimeout<kMultiWaitThreadCount>());
}

template <size_t N>
void TestPresignalWait() {
  sync_completion_t completion;
  std::array<TestThread, N> threads;

  // Start by signaling the completion initially.
  sync_completion_signal(&completion);

  // Start the threads
  for (auto& thread : threads) {
    ASSERT_NO_FATAL_FAILURES(thread.StartAndBlock("TestPresignalWait", &completion));
  }

  // Wait for the threads to finish, and verify that they received the proper
  // wait result.
  for (auto& thread : threads) {
    thread.Join(false);
    ASSERT_OK(thread.status());
  }
}

TEST(SyncCompletionTest, PresignalSingleWait) {
  ASSERT_NO_FATAL_FAILURES(TestPresignalWait<1>());
}

TEST(SyncCompletionTest, PresignalMultiWait) {
  ASSERT_NO_FATAL_FAILURES(TestPresignalWait<kMultiWaitThreadCount>());
}

template <size_t N>
void TestResetCycleWait() {
  sync_completion_t completion;
  std::array<TestThread, N> threads;

  // Start by signaling, and then resetting the completion initially.
  sync_completion_signal(&completion);
  sync_completion_reset(&completion);

  // Start the threads
  for (auto& thread : threads) {
    ASSERT_NO_FATAL_FAILURES(thread.StartAndBlock("TestResetCycleWait", &completion));
  }

  // Wait until all of the threads have blocked, then signal the completion.
  ASSERT_NO_FATAL_FAILURES(WaitForAllBlockedOnFutex(threads));
  sync_completion_signal(&completion);

  // Wait for the threads to finish, and verify that they received the proper
  // wait result.
  for (auto& thread : threads) {
    thread.Join(false);
    ASSERT_OK(thread.status());
  }
}

TEST(SyncCompletionTest, ResetCycleSingleWait) {
  ASSERT_NO_FATAL_FAILURES(TestResetCycleWait<1>());
}

TEST(SyncCompletionTest, ResetCycleMultiWait) {
  ASSERT_NO_FATAL_FAILURES(TestResetCycleWait<kMultiWaitThreadCount>());
}

// This test would flake if spurious wake ups from zx_futex_wake() were possible.
// However, the documentation states that "Zircon's implementation of
// futexes currently does not generate spurious wakeups itself". If this changes,
// this test could be relaxed to only assert that threads wake up in the end.
TEST(SyncCompletionTest, SignalRequeue) {
  sync_completion_t completion;
  std::array<TestThread, kMultiWaitThreadCount> threads;

  // Start the threads and have them block on the completion.
  for (auto& thread : threads) {
    ASSERT_NO_FATAL_FAILURES(
        thread.StartAndBlock("TestSignalRequeue", &completion, zx::time::infinite()));
  }

  // Wait until all the threads have become blocked.
  ASSERT_NO_FATAL_FAILURES(WaitForAllBlockedOnFutex(threads));

  // Move them over to a different futex using the re-queue hook.
  zx_futex_t futex = 0;
  sync_completion_signal_requeue(&completion, &futex, ZX_HANDLE_INVALID);

  // Wait for a bit and make sure no one has woken up yet.  Note that this
  // clearly cannot catch all possible failures here.  It is a best effort check
  // only.
  zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));

  // Requeue is an atomic action.  All of the threads should still be blocked on
  // a futex (the target futex this time);
  bool all_blocked;
  ASSERT_NO_FATAL_FAILURES(CheckAllBlockedOnFutex(threads, &all_blocked));
  ASSERT_TRUE(all_blocked);

  // Now, wake the threads via the requeued futex
  ASSERT_OK(zx_futex_wake(&futex, UINT32_MAX));

  // Wait for the threads to finish, and verify that they received the proper
  // wait result.
  for (auto& thread : threads) {
    thread.Join(false);
    ASSERT_OK(thread.status());
  }
}

TEST(SyncCompletionTest, SpuriousWakeupHandled) {
  sync_completion_t completion;
  std::array<TestThread, 1> threads;
  auto& thread = threads[0];

  // Start the test thread and wait until we know that it is blocked in the futex.
  ASSERT_NO_FATAL_FAILURES(thread.StartAndBlock("SpuriousWakeupHandled", &completion));
  ASSERT_NO_FATAL_FAILURES(WaitForAllBlockedOnFutex(threads));

  // Peek under the implementation hood into the completion_t implementation,
  // and wake any threads waiting on the internal futex.  This should simulate a
  // spurious futex wake.
  ASSERT_OK(zx_futex_wake(&completion.futex, UINT32_MAX));

  // Now wait some amount of time, and then check to see if our thread has set
  // the exiting flag.  Note that this is a best effort test only.  We are
  // attempting to prove that the thread has woken up, checked its internal
  // state, and gone back to sleep on the futex.  Unfortunately, because the
  // user-mode observable thread state is not atomically updated with the wake
  // operation (the lower level kernel-thread-state is, but we cannot observe
  // it), we cannot simply wait for the thread to block again and _then_ check
  // the exiting flag.  Instead, we have to pick a timeout and keep it
  // relatively short (100mSec in this case).  If this catches a problem, we
  // know it is a real problem, but if it doesn't, it is technically possible
  // that there is a problem, but it just was not detected because of timing.
  zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
  ASSERT_FALSE(thread.exited());

  // Now deliberately signal the thread, and wait for it to exit.  Check that it
  // set the exited flag on the way out.
  thread.Join(true);
  ASSERT_TRUE(thread.exited());

  // Final sanity checks and we are done.
  ASSERT_OK(thread.status());
}

}  // namespace
