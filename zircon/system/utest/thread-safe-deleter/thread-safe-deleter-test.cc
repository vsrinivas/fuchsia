// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/closure-queue/closure_queue.h>
#include <lib/fit/defer.h>
#include <lib/fit/function.h>

#include <zxtest/zxtest.h>

#include "lib/thread-safe-deleter/thread_safe_deleter.h"

namespace {

class ThreadSafeDeleterTest : public zxtest::Test {
 protected:
  ThreadSafeDeleterTest();

  void WaitForDeferHolderDestruction();

  async::Loop main_loop_;
  ClosureQueue main_queue_;
  async::Loop other_loop_;
  ClosureQueue other_queue_;

  std::mutex lock_;
  thrd_t destruction_thread_ = {};

  using DeferHolder = ThreadSafeDeleter<fit::deferred_action<fit::closure>>;
  std::optional<DeferHolder> defer_holder_;
};

void ThreadSafeDeleterTest::WaitForDeferHolderDestruction() {
  ZX_DEBUG_ASSERT(thrd_current() == main_queue_.dispatcher_thread());
  while (true) {
    // This must happen outside lock_.
    main_loop_.RunUntilIdle();
    {  // scope lock
      std::unique_lock<std::mutex> lock(lock_);
      if (destruction_thread_) {
        return;
      }
    }  // ~lock
    // TODO(dustingreen): Make OneShotEvent usable here, and switch to that.
    zx::nanosleep(zx::deadline_after(zx::msec(5)));
  }
}

ThreadSafeDeleterTest::ThreadSafeDeleterTest()
    : main_loop_(&kAsyncLoopConfigAttachToCurrentThread),
      main_queue_(main_loop_.dispatcher(), thrd_current()),
      other_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
  thrd_t other_thread;
  zx_status_t status = other_loop_.StartThread("other_loop", &other_thread);
  ZX_ASSERT(status == ZX_OK);
  other_queue_.SetDispatcher(other_loop_.dispatcher(), other_thread);
  defer_holder_.emplace(&main_queue_, fit::defer<fit::closure>([this] {
    thrd_t current_thread = thrd_current();
    std::lock_guard<std::mutex> lock(lock_);
    destruction_thread_ = current_thread;
  }));
}

TEST_F(ThreadSafeDeleterTest, DeleteHolderOnMainThread) {
  ZX_ASSERT(!destruction_thread_);
  other_queue_.Enqueue([this, defer_holder = std::move(*defer_holder_)]() mutable {
    ZX_DEBUG_ASSERT(thrd_current() == other_queue_.dispatcher_thread());
    // Must be stopped on its own dispatcher thread, so go ahead and take care of that now.
    other_queue_.StopAndClear();
    main_queue_.Enqueue([this, defer_holder = std::move(defer_holder)]() mutable {
      ZX_DEBUG_ASSERT(thrd_current() == main_queue_.dispatcher_thread());
      // ~defer_holder
    });
  });
  WaitForDeferHolderDestruction();
  ASSERT_EQ(destruction_thread_, thrd_current());
}

TEST_F(ThreadSafeDeleterTest, DeleteHolderOnOtherThread) {
  ZX_ASSERT(!destruction_thread_);
  other_queue_.Enqueue([this, defer_holder = std::move(*defer_holder_)]() mutable {
    ZX_DEBUG_ASSERT(thrd_current() != main_queue_.dispatcher_thread());
    ZX_DEBUG_ASSERT(thrd_current() == other_queue_.dispatcher_thread());
    // Must be stopped on its own dispatcher thread, so go ahead and take care of that now.
    other_queue_.StopAndClear();
    // ~defer_holder should Enqueue destruction of held fit::defer to
    // main_queue_.
  });
  WaitForDeferHolderDestruction();
  ASSERT_EQ(destruction_thread_, thrd_current());
}

}  // namespace
