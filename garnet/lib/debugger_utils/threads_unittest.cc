// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _ALL_SOURCE
#define _ALL_SOURCE // Enables thrd_create_with_name in <threads.h>.
#endif

#include <stddef.h>
#include <string>
#include <threads.h>
#include <zircon/syscalls/object.h>
#include <zircon/threads.h>
#include <lib/zx/event.h>
#include <lib/zx/process.h>
#include <lib/zx/time.h>

#include "garnet/lib/debugger_utils/jobs.h"
#include "garnet/lib/debugger_utils/threads.h"
#include "garnet/lib/debugger_utils/util.h"
#include "src/lib/fxl/logging.h"
#include "gtest/gtest.h"

namespace debugger_utils {
namespace {

// Threads should eventually suspend and resume. Leave it to the watchdog
// to handle problems.
constexpr zx::duration kThreadSuspendTimeout = zx::duration::infinite();
constexpr zx::duration kThreadRunningTimeout = zx::duration::infinite();

constexpr size_t kNumTestThreads = 10u;

int ThreadFunction(void* arg) {
  auto keep_running = reinterpret_cast<std::atomic_bool*>(arg);
  while (*keep_running) {
    zx::nanosleep(zx::deadline_after(zx::msec(100)));
  }
  return 0;
}

void ShutdownThreads(const std::vector<thrd_t>& threads,
                     std::atomic_bool* keep_running) {
  *keep_running = false;
  for (auto& thread : threads) {
    int result = thrd_join(thread, NULL);
    FXL_DCHECK(result == thrd_success);
  }
}

bool CreateThreads(size_t num_threads, std::vector<thrd_t>* threads,
                   std::atomic_bool* keep_running) {
  for (size_t i = 0; i < num_threads; ++i) {
    char name[sizeof("thread") + 10];
    snprintf(name, sizeof(name), "thread%zu", i);
    thrd_t thread;
    if (thrd_create_with_name(&thread, ThreadFunction, keep_running, name) !=
        thrd_success) {
      goto Fail;
    }
    threads->push_back(thread);
  }

  return true;

 Fail:
  ShutdownThreads(*threads, keep_running);
  return false;
}

void CheckThreadSuspended(const zx::thread& thread, const char* msg) {
  EXPECT_EQ(GetThreadOsState(thread), ZX_THREAD_STATE_SUSPENDED) << msg;
}

void CheckThreadRunning(const zx::thread& thread, const char* msg) {
  uint32_t state = GetThreadOsState(thread);
  // Our threads are either running or sleeping.
  EXPECT_TRUE(state == ZX_THREAD_STATE_RUNNING ||
              state == ZX_THREAD_STATE_BLOCKED_SLEEPING) << msg;
}

void WaitThreadsRunning(const std::vector<zx::thread>& threads,
                        const char* msg) {
  for (const auto& thread : threads) {
    EXPECT_EQ(ZX_OK, thread.wait_one(ZX_THREAD_RUNNING | ZX_THREAD_TERMINATED,
                                     zx::deadline_after(kThreadRunningTimeout),
                                     nullptr)) << msg;
    CheckThreadRunning(thread, msg);
  }
}

TEST(Threads, WithThreadSuspended) {
  std::atomic_bool keep_running{true};
  std::vector<thrd_t> threads;
  std::vector<zx::thread> zx_threads;

  ASSERT_TRUE(CreateThreads(1u, &threads, &keep_running));

  zx_threads.push_back(zx::thread(thrd_get_zx_handle(threads[0])));
  WaitThreadsRunning(zx_threads, "pre-suspend");

  EXPECT_EQ(ZX_OK, WithThreadSuspended(zx_threads[0], kThreadSuspendTimeout,
                                       [] (const zx::thread& thread) -> zx_status_t {
    CheckThreadSuspended(thread, "inside WithThreadSuspended");
    return ZX_OK;
  }));

  // When a thread's suspend token is closed it does not necessarily
  // return to the RUNNING state immediately,
  WaitThreadsRunning(zx_threads, "post-suspend");

  // Release the handle as it will be closed elsewhere.
  EXPECT_NE(ZX_HANDLE_INVALID, zx_threads[0].release());

  ShutdownThreads(threads, &keep_running);
}

TEST(Threads, WithAllThreadsSuspended) {
  std::atomic_bool keep_running{true};
  std::vector<thrd_t> threads;
  std::vector<zx::thread> zx_threads;

  ASSERT_TRUE(CreateThreads(kNumTestThreads, &threads, &keep_running));

  for (const auto& thread : threads) {
    zx_threads.push_back(zx::thread(thrd_get_zx_handle(thread)));
  }
  WaitThreadsRunning(zx_threads, "pre-suspend");

  EXPECT_EQ(ZX_OK, WithAllThreadsSuspended(
      zx_threads, kThreadSuspendTimeout,
      [&zx_threads] (const zx::thread& thread) -> zx_status_t {
    for (const auto& t : zx_threads) {
      CheckThreadSuspended(t, "inside WithAllThreadsSuspended");
    }
    return ZX_OK;
  }));

  // When a thread's suspend token is closed it does not necessarily
  // return to the RUNNING state immediately.
  WaitThreadsRunning(zx_threads, "post-suspend");

  // Release the handles as they will be closed elsewhere.
  for (auto& thread : zx_threads) {
    EXPECT_NE(ZX_HANDLE_INVALID, thread.release());
  }

  ShutdownThreads(threads, &keep_running);
}

}  // namespace
}  // namespace debugger_utils
