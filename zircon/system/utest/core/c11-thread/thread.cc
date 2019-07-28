// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <sched.h>
#include <threads.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>

#include <zxtest/zxtest.h>

namespace {

TEST(C11ThreadTest, ThreadLocalErrno) {
  constexpr int kNumThreads = 4;
  thrd_t thread[kNumThreads];

  struct Args {
    int thread_number;
    int final_errno;
  } args[kNumThreads];

  auto ThreadLocalHelper = [](void* arg) -> int {
    auto args = static_cast<Args*>(arg);
    errno = args->thread_number;
    zx::nanosleep(zx::deadline_after(zx::msec(100)));
    args->final_errno = errno;

    return args->thread_number;
  };

  for (int i = 0; i < kNumThreads; ++i) {
    args[i].thread_number = i;
    ASSERT_EQ(thrd_success,
              thrd_create_with_name(&thread[i], ThreadLocalHelper, &args[i], "c11 thread test"));
  }
  for (int i = 0; i < kNumThreads; ++i) {
    int return_value = 99;
    ASSERT_EQ(thrd_join(thread[i], &return_value), thrd_success);
    ASSERT_EQ(return_value, i);
    ASSERT_EQ(args[i].final_errno, i);
  }
}

TEST(C11ThreadTest, NullNameThreadShouldSucceed) {
  auto NameHelper = [](void* arg) -> int { return 0; };

  constexpr auto kNullArg = nullptr;
  constexpr auto kNullName = nullptr;
  constexpr int* kIgnoreRet = nullptr;
  thrd_t thread;
  ASSERT_EQ(thrd_create_with_name(&thread, NameHelper, kNullArg, kNullName), thrd_success);
  ASSERT_EQ(thrd_join(thread, kIgnoreRet), thrd_success);
}

TEST(C11ThreadTest, CreateAndVerifyThreadHandle) {
  constexpr int kRandomRet = 5;

  auto ThreadHandleHelper = [](void* arg) -> int {
    std::atomic<bool>* keep_running = static_cast<std::atomic<bool>*>(arg);
    while (*keep_running) {
      zx::nanosleep(zx::time::infinite_past());
    }
    return kRandomRet;
  };

  thrd_t thread;
  std::atomic<bool> keep_running = true;
  ASSERT_EQ(thrd_create(&thread, ThreadHandleHelper, &keep_running), thrd_success);
  zx_handle_t handle = thrd_get_zx_handle(thread);
  ASSERT_NE(handle, ZX_HANDLE_INVALID, "got invalid thread handle");
  // Prove this is a valid handle by duplicating it.
  zx_handle_t dup_handle;
  ASSERT_OK(zx_handle_duplicate(handle, ZX_RIGHT_SAME_RIGHTS, &dup_handle));
  ASSERT_OK(zx_handle_close(dup_handle), "failed to close duplicate handle");
  keep_running = false;
  int return_value;
  ASSERT_EQ(thrd_join(thread, &return_value), thrd_success);
  ASSERT_EQ(return_value, kRandomRet, "Incorrect return from thread");
}

TEST(C11ThreadTest, DetachedThreadKeepsRunning) {
  constexpr zx::duration kWaitEachIteration = zx::usec(10);
  constexpr zx::duration kWaitMax = zx::usec(20000);

  struct Args {
    std::atomic<bool> keep_running;
    std::atomic<bool> thread_done;
    std::atomic<int> thread_iterations;
    const zx::duration kWaitEachIteration;
  } args = {true, false, 0, kWaitEachIteration};

  auto HandleHelper = [](void* arg) -> int {
    Args* args = static_cast<Args*>(arg);

    while (args->keep_running.load()) {
      zx::nanosleep(zx::deadline_after(args->kWaitEachIteration));
      args->thread_iterations.fetch_add(1);
    }
    args->thread_done.store(true);
    return 0;
  };

  thrd_t thread;
  ASSERT_EQ(thrd_create(&thread, HandleHelper, &args), thrd_success);

  ASSERT_EQ(thrd_detach(thread), thrd_success);

  // observe the thread is still operating
  int recorded_thread_iterations = args.thread_iterations.load();
  zx::duration time_waited;
  while (recorded_thread_iterations == args.thread_iterations.load() && time_waited < kWaitMax) {
    zx::nanosleep(zx::deadline_after(kWaitEachIteration));
    time_waited += kWaitEachIteration;
  }
  ASSERT_EQ(args.thread_done.load(), false);

  args.keep_running.store(false);

  time_waited = zx::duration();
  while (!args.thread_done.load() && time_waited < kWaitMax) {
    zx::nanosleep(zx::deadline_after(kWaitEachIteration));
    time_waited += kWaitEachIteration;
  }
  ASSERT_TRUE(args.thread_done.load());
}

TEST(C11ThreadTest, LongNameSucceeds) {
  auto LongNameHelper = [](void* arg) { return 0; };

  // Creating a thread with a super long name should succeed.
  constexpr char kLongName[] =
      "0123456789012345678901234567890123456789"
      "0123456789012345678901234567890123456789";
  ASSERT_GT(strlen(kLongName), static_cast<size_t>(ZX_MAX_NAME_LEN - 1), "too short to truncate");

  thrd_t thread;
  ASSERT_EQ(thrd_create_with_name(&thread, LongNameHelper, nullptr, kLongName), thrd_success);

  // Clean up.
  EXPECT_EQ(thrd_join(thread, nullptr), thrd_success);
}

TEST(C11ThreadTest, SelfDetachAndFree) {
  constexpr int kNumThreads = 1000;
  std::atomic<int> num_threads_completed = 0;

  struct Args {
    thrd_t* thread;
    std::atomic<int> detach_status;
    std::atomic<int>* num_threads_completed;
  } args[kNumThreads];

  auto DetachHelper = [](void* arg) -> int {
    Args* args = static_cast<Args*>(arg);
    args->detach_status.store(thrd_detach(*args->thread));
    delete args->thread;
    args->num_threads_completed->fetch_add(1);
    return 0;
  };

  for (size_t i = 0; i < kNumThreads; i++) {
    args[i].thread = new thrd_t;
    args[i].num_threads_completed = &num_threads_completed;
    ASSERT_EQ(thrd_create(args[i].thread, DetachHelper, &args[i]), thrd_success);
  }
  while (num_threads_completed.load() != kNumThreads) {
    sched_yield();
  }
  for (size_t i = 0; i < kNumThreads; i++) {
    ASSERT_EQ(args[i].detach_status.load(), thrd_success);
  }
}

}  // namespace
