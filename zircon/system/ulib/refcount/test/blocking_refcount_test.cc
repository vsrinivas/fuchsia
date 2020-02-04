// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sync/completion.h>
#include <threads.h>

#include <refcount/blocking_refcount.h>
#include <zxtest/zxtest.h>

#include "lib/zx/time.h"

namespace refcount {
namespace {

// A simple C11 thread wrapper.
class Thread {
 public:
  Thread(std::function<void()> start) : start_(std::move(start)) {
    int result = thrd_create(
        &thread_,
        [](void* start_ptr) {
          auto start = static_cast<std::function<void()>*>(start_ptr);
          (*start)();
          return 0;
        },
        &start_);
    ZX_ASSERT(result == thrd_success);
  }

  void Join() {
    ZX_ASSERT(!joined_);
    int result;
    thrd_join(thread_, &result);
    joined_ = true;
  }

  ~Thread() {
    if (!joined_) {
      Join();
    }
  }

  // Disallow copy/move.
  Thread(const Thread&) = delete;
  Thread(Thread&&) = delete;
  Thread& operator=(Thread&&) = delete;
  Thread& operator=(const Thread&) = delete;

 private:
  bool joined_ = false;
  std::function<void()> start_;
  thrd_t thread_;
};

TEST(BlockingRefCount, WaitOnDefaultConstructed) {
  BlockingRefCount a;
  a.WaitForZero();
}

TEST(BlockingRefCount, NonDefaultValue) {
  BlockingRefCount a(2);
  a.Dec();
  a.Dec();
  a.WaitForZero();
}

TEST(BlockingRefCount, IncDecWait) {
  BlockingRefCount a;
  a.Inc();
  a.Dec();
  a.WaitForZero();
}

#if ZX_DEBUG_ASSERT_IMPLEMENTED
TEST(BlockingRefCount, AssertFailOnDecBelowZero) {
  BlockingRefCount a;
  ASSERT_DEATH(([&a] { a.Dec(); }), "Expected assert failure.");
}
#endif

#if ZX_DEBUG_ASSERT_IMPLEMENTED
TEST(BlockingRefCount, AssertFailOnIncOverflow) {
  BlockingRefCount a(INT32_MAX);
  ASSERT_DEATH(([&a] { a.Inc(); }), "Expected assert failure.");
}
#endif

TEST(BlockingRefCount, WakeUpThread) {
  BlockingRefCount a(1);
  sync_completion_t worker_started;
  sync_completion_t worker_woke_up;

  // Start a thread to block on the refcount.
  auto worker = Thread([&]() {
    sync_completion_signal(&worker_started);
    a.WaitForZero();
    sync_completion_signal(&worker_woke_up);
  });

  // Wait for worker to block.
  sync_completion_wait(&worker_started, ZX_TIME_INFINITE);

  // Give buggy workers a chance to keep running, but ensure that they
  // didn't.
  zx::nanosleep(zx::deadline_after(zx::msec(10)));
  EXPECT_FALSE(sync_completion_signaled(&worker_woke_up));

  // Wake up the worker.
  a.Dec();
  sync_completion_wait(&worker_woke_up, ZX_TIME_INFINITE);
}

}  // namespace
}  // namespace refcount
