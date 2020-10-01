// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/clock.h>
#include <lib/zx/event.h>
#include <stddef.h>
#include <threads.h>
#include <zircon/syscalls.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <array>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>

#include <fbl/auto_call.h>
#include <zxtest/zxtest.h>

namespace {

constexpr int* kNoReturnValue = nullptr;

class TestThread {
 public:
  TestThread(size_t number_tries, zx::duration delay, mtx_t* lock)
      : number_tries_(number_tries), delay_(delay), lock_(lock) {
    ZX_ASSERT(lock != nullptr);
  }

  void Start() {
    int status = thrd_create(
        &tid_,
        [](void* ctx) -> int {
          return reinterpret_cast<TestThread*>(ctx)->RepeatMutexLockAndUnlock();
        },
        this);

    started_ = status == thrd_success;
    ASSERT_TRUE(started_);
  }

  void Join() {
    if (started_) {
      thrd_join(tid_, kNoReturnValue);
      started_ = false;
    }
  }

  int RepeatMutexLockAndUnlock() {
    for (uint64_t tries = 0; tries < number_tries_; tries++) {
      mtx_lock(lock_);
      zx::nanosleep(zx::deadline_after(delay_));
      mtx_unlock(lock_);
    }
    return 0;
  }

 private:
  uint64_t number_tries_;
  zx::duration delay_;
  mtx_t* lock_;
  thrd_t tid_;
  bool started_ = false;
};

TEST(C11MutexTest, MultiThreadedContention) {
  // These tests all conditionally acquire the lock, by design. The
  // thread safety analysis is not up to this, so disable it.
  mtx_t lock;
  ASSERT_EQ(thrd_success, mtx_init(&lock, mtx_timed));

  std::array threads = {
      TestThread{300, zx::usec(100), &lock},
      TestThread{150, zx::usec(200), &lock},
      TestThread{100, zx::usec(300), &lock},
  };

  for (auto& thread : threads) {
    thread.Start();
    ASSERT_NO_FAILURES();
  }
  for (auto& thread : threads) {
    thread.Join();
    ASSERT_NO_FAILURES();
  }
}

// TODO(fxbug.dev/39408) Remove all TA_NO_THREAD_SAFETY_ANALYSIS annotations when we can
TEST(C11MutexTest, TryMutexMultiThreadedContention) TA_NO_THREAD_SAFETY_ANALYSIS {
  struct MutexThreadArgs {
    std::atomic<int> lock_acquired;
    std::atomic<int> lock_released = thrd_error;
    mtx_t* lock;
  } args;

  // This test conditionally acquires the lock, by design. The
  // thread safety analysis is not up to this, so disable it.
  auto TryGrabLock = [](void* arg) TA_NO_THREAD_SAFETY_ANALYSIS -> int {
    MutexThreadArgs* args = static_cast<MutexThreadArgs*>(arg);

    args->lock_acquired = mtx_trylock(args->lock);

    if (args->lock_acquired == thrd_success) {
      args->lock_released = mtx_unlock(args->lock);
    }

    return 0;
  };

  mtx_t lock;
  bool lock_held = false;
  ASSERT_EQ(thrd_success, mtx_init(&lock, mtx_plain));
  args.lock = &lock;
  auto cleanup_mutex = fbl::MakeAutoCall([&]() TA_NO_THREAD_SAFETY_ANALYSIS {
    if (lock_held) {
      mtx_unlock(&lock);
    }
    mtx_destroy(&lock);
  });

  thrd_t thread_id;

  ASSERT_EQ(thrd_success, mtx_lock(&lock));
  lock_held = true;
  ASSERT_EQ(thrd_success, thrd_create(&thread_id, TryGrabLock, &args));
  ASSERT_EQ(thrd_success, thrd_join(thread_id, kNoReturnValue));

  ASSERT_EQ(thrd_success, mtx_unlock(&lock));
  lock_held = false;

  ASSERT_EQ(thrd_busy, args.lock_acquired);

  args.lock_acquired = thrd_error;
  args.lock_released = thrd_error;
  ASSERT_EQ(thrd_success, thrd_create(&thread_id, TryGrabLock, &args));
  ASSERT_EQ(thrd_success, thrd_join(thread_id, kNoReturnValue));
  ASSERT_EQ(thrd_success, args.lock_acquired);
  ASSERT_EQ(thrd_success, args.lock_released);
}

TEST(C11MutexTest, InitalizeLocalMutex) {
  mtx_t auto_mutex;
  ASSERT_EQ(thrd_success, mtx_init(&auto_mutex, mtx_timed));
  mtx_destroy(&auto_mutex);
}

TEST(C11MutexTest, StaticInitalizerSameBytesAsAuto) {
  static mtx_t static_mutex = MTX_INIT;
  mtx_t auto_mutex;
  memset(&auto_mutex, 0xae, sizeof(auto_mutex));
  mtx_init(&auto_mutex, mtx_plain);
  auto cleanup_mutex = fbl::MakeAutoCall([&]() { mtx_destroy(&auto_mutex); });

  EXPECT_BYTES_EQ(reinterpret_cast<uint8_t*>(&static_mutex),
                  reinterpret_cast<uint8_t*>(&auto_mutex), sizeof(mtx_t),
                  "MTX_INIT and mtx_init differ!");
}

TEST(C11MutexTest, TimeoutElapsed) {
  struct ThreadTimeoutArgs {
    mtx_t lock;
    zx::event start_event;
    zx::event done_event;
    std::atomic<bool> mtx_lock_result = false;
    std::atomic<bool> mtx_unlock_result = false;
    std::atomic<zx_status_t> signal_result = ZX_ERR_INTERNAL;
    std::atomic<zx_status_t> wait_one_result = ZX_ERR_INTERNAL;
  };

  auto TestTimeoutHelper = [](void* ctx) TA_NO_THREAD_SAFETY_ANALYSIS -> int {
    ThreadTimeoutArgs* args = static_cast<ThreadTimeoutArgs*>(ctx);

    if ((args->mtx_lock_result = mtx_lock(&args->lock)) != thrd_success) {
      return -1;
    }

    // Inform the main thread that we have acquired the lock.
    if ((args->signal_result = args->start_event.signal(0, ZX_EVENT_SIGNALED)) != ZX_OK) {
      mtx_unlock(&args->lock);
      return -1;
    }

    // Wait until the main thread has completed its test.
    if ((args->wait_one_result = args->done_event.wait_one(ZX_EVENT_SIGNALED, zx::time::infinite(),
                                                           nullptr)) != ZX_OK) {
      mtx_unlock(&args->lock);
      return -1;
    }

    if ((args->mtx_unlock_result = mtx_unlock(&args->lock)) != thrd_success) {
      return -1;
    }
    return 0;
  };

  constexpr zx::duration kRelativeDeadline = zx::msec(100);

  ThreadTimeoutArgs args;
  ASSERT_EQ(thrd_success, mtx_init(&args.lock, mtx_plain));
  auto cleanup_mutex = fbl::MakeAutoCall([&]() { mtx_destroy(&args.lock); });
  ASSERT_OK(zx::event::create(0, &args.start_event));
  ASSERT_OK(zx::event::create(0, &args.done_event));

  thrd_t helper;
  ASSERT_EQ(thrd_create(&helper, TestTimeoutHelper, &args), thrd_success);

  cleanup_mutex.cancel();
  auto cleanup_thread = fbl::MakeAutoCall([&]() {
    args.done_event.signal(0, ZX_EVENT_SIGNALED);
    thrd_join(helper, kNoReturnValue);
    mtx_destroy(&args.lock);
  });

  // Wait for the helper thread to acquire the lock.
  ASSERT_OK(args.start_event.wait_one(ZX_EVENT_SIGNALED, zx::time::infinite(), nullptr));

  for (int i = 0; i < 5; ++i) {
    zx::time_utc now;
    ASSERT_OK(zx::clock::get(&now));
    struct timespec then = {
        .tv_sec = now.get() / ZX_SEC(1),
        .tv_nsec = now.get() % ZX_SEC(1),
    };
    then.tv_nsec += kRelativeDeadline.get();
    if (then.tv_nsec > reinterpret_cast<long>(ZX_SEC(1))) {
      then.tv_nsec -= ZX_SEC(1);
      then.tv_sec += 1;
    }
    int rc = mtx_timedlock(&args.lock, &then);
    ASSERT_EQ(rc, thrd_timedout, "wait should time out");
    zx::time_utc later;

    ASSERT_OK(zx::clock::get(&later));
    zx_duration_t elapsed = later.get() - now.get();
    // Since the wait is based on the UTC clock which can be adjusted while
    // the wait proceeds, it is important to check that the mutex does not
    // return early.
    // TODO(fxbug.dev/34941): when the kernel UTC clock gets wired, the test framework should
    // expose a way to do local modifications to the clock so we can properly
    // test this behavior. Currently the UTC offset is global and mutating it here
    // can create hard to diagnose flakes as seen with bug 34857.
    EXPECT_GE(elapsed, kRelativeDeadline.get(), "wait returned early");
  }

  // Inform the helper thread that we are done.
  ASSERT_OK(args.done_event.signal(0, ZX_EVENT_SIGNALED));
  ASSERT_EQ(thrd_join(helper, kNoReturnValue), thrd_success);
  cleanup_thread.cancel();

  mtx_destroy(&args.lock);

  ASSERT_EQ(args.mtx_lock_result, thrd_success);
  ASSERT_OK(args.signal_result);
  ASSERT_OK(args.wait_one_result);
  ASSERT_EQ(args.mtx_unlock_result, thrd_success);
}

}  // namespace
