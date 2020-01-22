// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <sched.h>
#include <threads.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>

#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/futex.h>
#include <lib/zx/clock.h>
#include <lib/zx/suspend_token.h>
#include <lib/zx/thread.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>
#include <zircon/time.h>
#include <zircon/types.h>
#include <zxtest/zxtest.h>

namespace futex {
namespace {
constexpr zx::duration kDefaultTimeout = zx::sec(5);
constexpr zx::duration kDefaultPollInterval = zx::usec(100);

constexpr uint32_t kThreadWakeAllCount = std::numeric_limits<uint32_t>::max();
constexpr char kThreadName[] = "wakeup-test-thread";

// Poll until the user provided Callable |should_stop| tells us to stop by
// returning true.
template <typename Callable>
zx_status_t WaitFor(const Callable& should_stop, zx::duration timeout = kDefaultTimeout,
                    zx::duration poll_interval = kDefaultPollInterval) {
  static_assert(std::is_same_v<decltype(should_stop()), bool>, "should_stop() must return a bool!");

  zx::time deadline = zx::deadline_after(timeout);
  zx::time now;

  while ((now = zx::clock::get_monotonic()) < deadline) {
    if (should_stop()) {
      return ZX_OK;
    }

    zx::nanosleep(zx::deadline_after(poll_interval));
  }

  return ZX_ERR_TIMED_OUT;
}

void GetThreadState(const zx::thread& thread, zx_thread_state_t* out_state) {
  zx_info_thread_t info;

  ASSERT_NOT_NULL(out_state);
  ASSERT_TRUE(thread.is_valid());
  ASSERT_OK(thread.get_info(ZX_INFO_THREAD, &info, sizeof(info), nullptr, nullptr));
  *out_state = info.state;
}

void WaitForKernelState(const zx::thread& thread, zx_thread_state_t target_state,
                        zx::duration timeout = zx::duration::infinite()) {
  zx_status_t wait_res = ZX_ERR_INTERNAL;
  zx_thread_state_t state = 0;

  wait_res = WaitFor([&]() {
    GetThreadState(thread, &state);
    // Stop if we have hit the state we want, or we have an error attempting
    // to fetch our kernel thread state.
    return (state == target_state);
  });

  EXPECT_OK(wait_res);
  // Verify that any of the helpers methods called has no assertion failures.
  ASSERT_NO_FATAL_FAILURES();
  ASSERT_EQ(state, target_state);
}

class TestThread {
 public:
  TestThread() = default;
  TestThread(const TestThread&) = delete;
  TestThread(TestThread&&) = delete;
  TestThread& operator=(const TestThread&) = delete;
  TestThread& operator=(TestThread&&) = delete;
  ~TestThread() { Shutdown(); }

  void Start(zx_futex_t* futex, zx::duration timeout = zx::duration::infinite()) {
    ASSERT_FALSE(thread_handle_.is_valid(), "Attempting to start already started thread.");

    futex_.store(futex);
    timeout_ = timeout;
    wait_result_.store(ZX_ERR_INTERNAL);

    ASSERT_EQ(
        thrd_create_with_name(
            &thread_,
            [](void* thread_args) { return reinterpret_cast<TestThread*>(thread_args)->Run(); },
            this, kThreadName),
        thrd_success, "Thread creation failed.");

    // Make a copy of our thread's handle so that we have something to query
    // re: the thread's status, even if the thread exits out from under us
    // (which will invalidate the handled returned by thrd_get_zx_handle
    ASSERT_OK(zx::unowned_thread(thrd_get_zx_handle(thread_))
                  ->duplicate(ZX_RIGHT_SAME_RIGHTS, &thread_handle_));

    EXPECT_OK(WaitFor([this]() { return state() != State::kWaitingToStart; }));

    // Note that this could fail if futex_wait() gets a spurious wakeup.
    EXPECT_EQ(state(), State::kAboutToWait, "Wrong thread state.");

    // We should only do this after state_ is State::kAboutToWait,
    // otherwise it could return when the thread has temporarily
    // blocked on a libc-internal futex.
    ASSERT_NO_FATAL_FAILURES(WaitForKernelState(thread_handle_, ZX_THREAD_STATE_BLOCKED_FUTEX));

    // This could also fail if futex_wait() gets a spurious wakeup.
    EXPECT_EQ(state(), State::kAboutToWait, "Wrong thread state.");
  }

  void Shutdown() {
    if (thread_handle_.is_valid()) {
      zx_status_t res =
          thread_handle_.wait_one(ZX_THREAD_TERMINATED, zx::deadline_after(zx::sec(10)), nullptr);
      ASSERT_OK(res, "Thread did not terminate in a timely fashion!");
      EXPECT_EQ(thrd_join(thread_, nullptr), thrd_success, "thrd_join failed");
      thread_handle_.reset();
    }
  }

  void WaitUntilWoken() const {
    ASSERT_OK(WaitFor([this]() { return state() == State::kWaitReturned; }));
    ASSERT_EQ(state(), State::kWaitReturned, "Thread in wrong state");
  }

  void CheckIsBlockedOnFutex() const {
    zx_thread_state_t state;
    ASSERT_NO_FATAL_FAILURES(GetThreadState(thread_handle_, &state));
    ASSERT_EQ(state, ZX_THREAD_STATE_BLOCKED_FUTEX);
  }

  const zx::thread& thread() const { return thread_handle_; }
  bool HasWaitReturned() const { return state() == State::kWaitReturned; }
  zx_status_t wait_result() const { return wait_result_.load(); }

 private:
  enum class State {
    kWaitingToStart = 100,
    kAboutToWait = 200,
    kWaitReturned = 300,
  };

  int Run() {
    state_.store(State::kAboutToWait);

    zx::time deadline = zx::deadline_after(timeout_);
    wait_result_.store(zx_futex_wait(futex(), *futex(), ZX_HANDLE_INVALID, deadline.get()));
    state_.store(State::kWaitReturned);
    return 0;
  }

  State state() const { return state_.load(); }
  zx_futex_t* futex() const { return futex_.load(); }

  std::atomic<zx_status_t> wait_result_{ZX_ERR_INTERNAL};
  std::atomic<zx_futex_t*> futex_{nullptr};
  std::atomic<State> state_{State::kWaitingToStart};
  zx::duration timeout_ = zx::duration::infinite();
  zx::thread thread_handle_;
  thrd_t thread_;
};

void AssertWokeThreadCount(const TestThread threads[], uint32_t total_thread_count,
                           uint32_t target_woke_count) {
  ASSERT_LE(target_woke_count, total_thread_count);

  auto CountWoken = [&threads, total_thread_count]() -> uint32_t {
    uint32_t ret = 0;
    for (uint32_t i = 0; i < total_thread_count; ++i) {
      if (threads[i].HasWaitReturned()) {
        ++ret;
      }
    }
    return ret;
  };

  // Wait forever until we achieve the target count.  If threads are not
  // waking up as they should, the test framework should eventually kill
  // us.
  uint32_t woken;
  do {
    woken = CountWoken();
  } while (woken < target_woke_count);

  ASSERT_EQ(CountWoken(), target_woke_count);

  // Wait an arbitrary amount of time to be sure that no one else wakes
  // up.
  //
  // TODO(johngro) : It would be really nice if we didn't have to have an
  // arbitrary wait here.  Unfortunately, I'm not sure that there is any
  // amount of time that we can wait and prove that a thread might not
  // spuriously wake up in the future.
  zx::nanosleep(zx::deadline_after(zx::msec(300)));
  ASSERT_EQ(CountWoken(), target_woke_count);
}

TEST(FutexTest, WaitValueMismatch) {
  int32_t futex_value = 123;
  ASSERT_EQ(zx_futex_wait(&futex_value, futex_value + 1, ZX_HANDLE_INVALID, ZX_TIME_INFINITE),
            ZX_ERR_BAD_STATE, "Futex wait should have reurned bad state");
}

TEST(FutexTest, WaitTimeout) {
  int32_t futex_value = 123;

  ASSERT_EQ(zx_futex_wait(&futex_value, futex_value, ZX_HANDLE_INVALID, 0), ZX_ERR_TIMED_OUT,
            "Futex wait should have reurned timeout");
}

// This test checks that the timeout in futex_wait() is respected
TEST(FutexTest, WaitTimeoutElapsed) {
  int32_t futex_value = 0;
  constexpr zx::duration kRelativeDeadline = zx::msec(100);

  for (int i = 0; i < 5; ++i) {
    zx::time deadline = zx::deadline_after(kRelativeDeadline);

    ASSERT_EQ(zx_futex_wait(&futex_value, 0, ZX_HANDLE_INVALID, deadline.get()), ZX_ERR_TIMED_OUT,
              "wait should time out");
    EXPECT_GE(zx::clock::get_monotonic().get(), deadline.get(), "wait returned early");
  }
}

TEST(FutexTest, WaitBadAddress) {
  // Check that the wait address is checked for validity.
  ASSERT_EQ(zx_futex_wait(nullptr, 123, ZX_HANDLE_INVALID, ZX_TIME_INFINITE), ZX_ERR_INVALID_ARGS,
            "Futex wait should have reurned invalid_arg");
}

// Test that we can wake up a single thread.
TEST(FutexTest, Wakeup) {
  fbl::futex_t futex_value(1);
  TestThread thread;

  ASSERT_NO_FATAL_FAILURES(thread.Start(&futex_value));

  // Clean up on exit.
  auto cleanup = fbl::MakeAutoCall([&thread, &futex_value]() {
    EXPECT_OK(zx_futex_wake(&futex_value, kThreadWakeAllCount));
    thread.Shutdown();
  });

  ASSERT_OK(zx_futex_wake(&futex_value, kThreadWakeAllCount));
  ASSERT_NO_FATAL_FAILURES(thread.WaitUntilWoken());
  ASSERT_OK(thread.wait_result());
}

// Test that we can wake up multiple threads, and that futex_wake() heeds
// the wakeup limit.
TEST(FutexTest, WakeupLimit) {
  constexpr int kWakeCount = 2;
  fbl::futex_t futex_value(1);
  TestThread threads[4];

  // If something goes wrong and we bail out early, do our best to shut down as cleanly as we
  auto cleanup = fbl::MakeAutoCall([&]() {
    zx_futex_wake(&futex_value, kThreadWakeAllCount);
    for (auto& t : threads) {
      t.Shutdown();
    }
  });

  for (auto& t : threads) {
    ASSERT_NO_FATAL_FAILURES(t.Start(&futex_value));
  }

  ASSERT_OK(zx_futex_wake(&futex_value, kWakeCount));

  // Test that exactly |kWakeCount| threads wake up from the queue.  We do not know
  // which threads are going to wake up, just that two threads are going to
  // wake up.
  ASSERT_NO_FATAL_FAILURES(AssertWokeThreadCount(threads, fbl::count_of(threads), 2));

  // Clean up: Wake the remaining threads so that they can exit.
  ASSERT_OK(zx_futex_wake(&futex_value, kThreadWakeAllCount));
  ASSERT_NO_FATAL_FAILURES(
      AssertWokeThreadCount(threads, fbl::count_of(threads), fbl::count_of(threads)));

  for (auto& t : threads) {
    ASSERT_OK(t.wait_result());
    ASSERT_NO_FATAL_FAILURES(t.Shutdown());
  }

  cleanup.cancel();
}

// Check that futex_wait() and futex_wake() heed their address arguments
// properly.  A futex_wait() call on one address should not be woken by a
// futex_wake() call on another address.
TEST(FutexTest, WakeupAddress) {
  fbl::futex_t futex_value1(1);
  fbl::futex_t futex_value2(1);
  fbl::futex_t dummy_value(1);
  TestThread threads[2];

  // If something goes wrong and we bail out early, do our best to shut down as cleanly as we can.
  auto cleanup = fbl::MakeAutoCall([&]() {
    zx_futex_wake(&futex_value1, kThreadWakeAllCount);
    zx_futex_wake(&futex_value2, kThreadWakeAllCount);
    for (auto& t : threads) {
      t.Shutdown();
    }
  });

  ASSERT_NO_FATAL_FAILURES(threads[0].Start(&futex_value1));
  ASSERT_NO_FATAL_FAILURES(threads[1].Start(&futex_value2));

  ASSERT_OK(zx_futex_wake(&dummy_value, kThreadWakeAllCount));
  ASSERT_NO_FATAL_FAILURES(threads[0].CheckIsBlockedOnFutex());
  ASSERT_NO_FATAL_FAILURES(threads[1].CheckIsBlockedOnFutex());

  ASSERT_OK(zx_futex_wake(&futex_value1, kThreadWakeAllCount));
  ASSERT_NO_FATAL_FAILURES(threads[0].WaitUntilWoken());
  ASSERT_NO_FATAL_FAILURES(threads[1].CheckIsBlockedOnFutex());

  // Clean up: Wake the remaining thread so that it can exit.
  ASSERT_OK(zx_futex_wake(&futex_value2, kThreadWakeAllCount));
  ASSERT_NO_FATAL_FAILURES(threads[1].WaitUntilWoken());

  for (auto& t : threads) {
    ASSERT_OK(t.wait_result());
    ASSERT_NO_FATAL_FAILURES(t.Shutdown());
  }

  cleanup.cancel();
}

TEST(FutexTest, RequeueValueMismatch) {
  zx_futex_t futex_value1 = 100;
  zx_futex_t futex_value2 = 200;

  ASSERT_EQ(
      zx_futex_requeue(&futex_value1, 1, futex_value1 + 1, &futex_value2, 1, ZX_HANDLE_INVALID),
      ZX_ERR_BAD_STATE, "requeue should have returned bad state");
}

TEST(FutexTest, RequeueSameAddr) {
  zx_futex_t futex_value = 100;

  ASSERT_EQ(zx_futex_requeue(&futex_value, 1, futex_value, &futex_value, 1, ZX_HANDLE_INVALID),
            ZX_ERR_INVALID_ARGS, "requeue should have returned invalid args");
}

// Test that futex_requeue() can wake up some threads and requeue others.
TEST(FutexTest, Requeue) {
  fbl::futex_t futex_value1(100);
  fbl::futex_t futex_value2(200);
  TestThread threads[6];

  // If something goes wrong and we bail out early, do our best to shut down as cleanly as we
  auto cleanup = fbl::MakeAutoCall([&]() {
    zx_futex_wake(&futex_value1, kThreadWakeAllCount);
    zx_futex_wake(&futex_value2, kThreadWakeAllCount);
    for (auto& t : threads) {
      t.Shutdown();
    }
  });

  for (auto& t : threads) {
    ASSERT_NO_FATAL_FAILURES(t.Start(&futex_value1));
  }

  ASSERT_OK(zx_futex_requeue(&futex_value1, 3, 100, &futex_value2, 2, ZX_HANDLE_INVALID));

  // 3 of the threads should have been woken.
  ASSERT_NO_FATAL_FAILURES(AssertWokeThreadCount(threads, countof(threads), 3));

  // Since 2 of the threads should have been requeued, waking all the
  // threads on futex_value2 should wake 2 more threads.
  ASSERT_OK(zx_futex_wake(&futex_value2, kThreadWakeAllCount));
  ASSERT_NO_FATAL_FAILURES(AssertWokeThreadCount(threads, countof(threads), 5));

  // Clean up: Wake the remaining thread so that it can exit.
  ASSERT_OK(zx_futex_wake(&futex_value1, 1));
  ASSERT_NO_FATAL_FAILURES(AssertWokeThreadCount(threads, countof(threads), countof(threads)));

  for (auto& t : threads) {
    ASSERT_NO_FATAL_FAILURES(t.Shutdown());
  }

  cleanup.cancel();
}

// Test the case where futex_wait() times out after having been moved to a
// different queue by futex_requeue().  Check that futex_wait() removes
// itself from the correct queue in that case.
TEST(FutexTest, RequeueUnqueuedOnTimeout) {
  fbl::futex_t futex_value1(100);
  fbl::futex_t futex_value2(200);
  TestThread threads[2];

  // If something goes wrong and we bail out early, do our best to shut down as cleanly as we
  auto cleanup = fbl::MakeAutoCall([&]() {
    zx_futex_wake(&futex_value1, kThreadWakeAllCount);
    zx_futex_wake(&futex_value2, kThreadWakeAllCount);
    for (auto& t : threads) {
      t.Shutdown();
    }
  });

  ASSERT_NO_FATAL_FAILURES(threads[0].Start(&futex_value1, zx::msec(300)));
  ASSERT_OK(zx_futex_requeue(&futex_value1, 0, 100, &futex_value2, kThreadWakeAllCount,
                             ZX_HANDLE_INVALID));
  ASSERT_NO_FATAL_FAILURES(threads[1].Start(&futex_value2));

  // thread 0 and 1 should now both be waiting on futex_value2.  Thread 0
  // should timeout in a short while, but thread 1 should still be waiting.

  ASSERT_NO_FATAL_FAILURES(threads[0].WaitUntilWoken());
  ASSERT_EQ(threads[0].wait_result(), ZX_ERR_TIMED_OUT);
  ASSERT_NO_FATAL_FAILURES(threads[1].CheckIsBlockedOnFutex());

  // thread 0 should have removed itself from futex_value2's wait queue,
  // so only thread 1 should be waiting on futex_value2.  We can test that
  // by doing futex_wake() with count=1.
  ASSERT_OK(zx_futex_wake(&futex_value2, 1));
  ASSERT_NO_FATAL_FAILURES(threads[1].WaitUntilWoken());

  for (auto& t : threads) {
    ASSERT_NO_FATAL_FAILURES(t.Shutdown());
  }

  cleanup.cancel();
}

// Test that the futex_wait() syscall is restarted properly if the thread
// calling it gets suspended and resumed.  (This tests for a bug where the
// futex_wait() syscall would return ZX_ERR_TIMED_OUT and not get restarted by
// the syscall wrapper in the VDSO.)
TEST(FutexTest, ThreadSuspended) {
  fbl::futex_t futex_value1(1);

  TestThread thread;

  // If something goes wrong and we bail out early, do our best to shut down as cleanly as we
  auto cleanup = fbl::MakeAutoCall([&]() {
    zx_futex_wake(&futex_value1, kThreadWakeAllCount);
    thread.Shutdown();
  });

  ASSERT_NO_FATAL_FAILURES(thread.Start(&futex_value1));

  zx::suspend_token suspend_token;
  ASSERT_OK(thread.thread().suspend(&suspend_token));

  // Wait until the thread is suspended.
  ASSERT_NO_FATAL_FAILURES(WaitForKernelState(thread.thread(), ZX_THREAD_STATE_SUSPENDED));
  ASSERT_OK(zx_handle_close(suspend_token.release()));

  // Wait some time for the thread to resume and execute.
  ASSERT_NO_FATAL_FAILURES(WaitForKernelState(thread.thread(), ZX_THREAD_STATE_BLOCKED_FUTEX));
  ASSERT_NO_FATAL_FAILURES(thread.CheckIsBlockedOnFutex());

  ASSERT_OK(zx_futex_wake(&futex_value1, 1));
  ASSERT_NO_FATAL_FAILURES(AssertWokeThreadCount(&thread, 1, 1));
  ASSERT_NO_FATAL_FAILURES(thread.Shutdown());

  cleanup.cancel();
}

// Test that misaligned pointers cause futex syscalls to return a failure.
TEST(FutexTest, MisalignedFutextAddr) {
  // Make sure the whole thing is aligned, so the 'futex' member will
  // definitely be misaligned.
  alignas(zx_futex_t) struct {
    uint8_t misalign;
    zx_futex_t futex[2];
  } __attribute__((packed)) buffer;

  zx_futex_t* const futex = &buffer.futex[0];
  zx_futex_t* const futex_2 = &buffer.futex[1];

  ASSERT_GT(alignof(zx_futex_t), 1);
  ASSERT_NE(reinterpret_cast<uintptr_t>(futex) % alignof(zx_futex_t), 0);
  ASSERT_NE(reinterpret_cast<uintptr_t>(futex_2) % alignof(zx_futex_t), 0);

  // zx_futex_requeue might check the waited-for value before it
  // checks the second futex's alignment, so make sure the call is
  // valid other than the alignment.  (Also don't ask anybody to
  // look at uninitialized stack space!)
  memset(&buffer, 0, sizeof(buffer));

  ASSERT_EQ(zx_futex_wait(futex, 0, ZX_HANDLE_INVALID, ZX_TIME_INFINITE), ZX_ERR_INVALID_ARGS);
  ASSERT_EQ(zx_futex_wake(futex, 1), ZX_ERR_INVALID_ARGS);
  ASSERT_EQ(zx_futex_requeue(futex, 1, 0, futex_2, 1, ZX_HANDLE_INVALID), ZX_ERR_INVALID_ARGS);
}

void log(const char* str) {
  zx::time now = zx::clock::get_monotonic();
  fprintf(stderr, "[%08" PRIu64 ".%08" PRIu64 "]: %s", now.get() / 1000000000,
          now.get() % 1000000000, str);
}

class Event {
 public:
  Event() : signaled_(0) {}

  void Wait() {
    if (signaled_ == 0) {
      zx_futex_wait(&signaled_, signaled_, ZX_HANDLE_INVALID, ZX_TIME_INFINITE);
    }
  }

  void Signal() {
    if (signaled_ == 0) {
      signaled_ = 1;
      zx_futex_wake(&signaled_, kThreadWakeAllCount);
    }
  }

 private:
  int32_t signaled_;
};

void WaitUntilThreadBlockedOnFutex(thrd_t thread) {
  zx_handle_t thrd_handle = thrd_get_zx_handle(thread);
  ASSERT_NE(thrd_handle, ZX_HANDLE_INVALID);

  zx_info_thread_t info;
  zx_status_t get_info_res = ZX_ERR_INTERNAL;
  zx_status_t wait_res = ZX_ERR_INTERNAL;

  wait_res = WaitFor([&]() {
    get_info_res =
        zx_object_get_info(thrd_handle, ZX_INFO_THREAD, &info, sizeof(info), nullptr, nullptr);
    return (get_info_res != ZX_OK) || (info.state == ZX_THREAD_STATE_BLOCKED_FUTEX);
  });

  EXPECT_OK(get_info_res);
  EXPECT_OK(wait_res);
  EXPECT_EQ(info.state, ZX_THREAD_STATE_BLOCKED_FUTEX);
}

TEST(FutexTest, EventSignaling) {
  thrd_t thread1, thread2, thread3;
  Event event;

  log("starting signal threads\n");
  thrd_create_with_name(
      &thread1,
      [](void* ctx) {
        Event* event = reinterpret_cast<Event*>(ctx);
        log("thread 1 waiting on event\n");
        event->Wait();
        log("thread 1 done\n");
        return 0;
      },
      &event, "thread 1");
  thrd_create_with_name(
      &thread2,
      [](void* ctx) {
        Event* event = reinterpret_cast<Event*>(ctx);
        log("thread 2 waiting on event\n");
        event->Wait();
        log("thread 2 done\n");
        return 0;
      },
      &event, "thread 2");
  thrd_create_with_name(
      &thread3,
      [](void* ctx) {
        Event* event = reinterpret_cast<Event*>(ctx);
        log("thread 3 waiting on event\n");
        event->Wait();
        log("thread 3 done\n");
        return 0;
      },
      &event, "thread 3");

  ASSERT_NO_FATAL_FAILURES(WaitUntilThreadBlockedOnFutex(thread1));
  ASSERT_NO_FATAL_FAILURES(WaitUntilThreadBlockedOnFutex(thread2));
  ASSERT_NO_FATAL_FAILURES(WaitUntilThreadBlockedOnFutex(thread3));

  log("signaling event\n");
  event.Signal();

  log("joining signal threads\n");
  thrd_join(thread1, nullptr);
  log("signal_thread 1 joined\n");
  thrd_join(thread2, nullptr);
  log("signal_thread 2 joined\n");
  thrd_join(thread3, nullptr);
  log("signal_thread 3 joined\n");
}

}  // namespace
}  // namespace futex
