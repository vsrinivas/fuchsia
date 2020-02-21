// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <lib/sync/mutex.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <zircon/syscalls.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <zxtest/zxtest.h>

static sync_mutex_t g_mutex;

static void xlog(const char* str) {
  zx_time_t now = zx_clock_get_monotonic();
  printf("[%08" PRIu64 ".%08" PRIu64 "]: %s", now / 1000000000, now % 1000000000, str);
}

static int mutex_thread_1(void* arg) {
  xlog("thread 1 started\n");

  for (int times = 0; times < 300; times++) {
    sync_mutex_lock(&g_mutex);
    zx_nanosleep(zx_deadline_after(ZX_USEC(1)));
    sync_mutex_unlock(&g_mutex);
  }

  xlog("thread 1 done\n");
  return 0;
}

static int mutex_thread_2(void* arg) {
  xlog("thread 2 started\n");

  for (int times = 0; times < 150; times++) {
    sync_mutex_lock(&g_mutex);
    zx_nanosleep(zx_deadline_after(ZX_USEC(2)));
    sync_mutex_unlock(&g_mutex);
  }

  xlog("thread 2 done\n");
  return 0;
}

static int mutex_thread_3(void* arg) {
  xlog("thread 3 started\n");

  for (int times = 0; times < 100; times++) {
    sync_mutex_lock(&g_mutex);
    zx_nanosleep(zx_deadline_after(ZX_USEC(3)));
    sync_mutex_unlock(&g_mutex);
  }

  xlog("thread 3 done\n");
  return 0;
}

static bool got_lock_1 = false;
static bool got_lock_2 = false;
static bool got_lock_3 = false;

// These tests all conditionally acquire the lock, by design. The
// thread safety analysis is not up to this, so disable it.
static int mutex_try_thread_1(void* arg) TA_NO_THREAD_SAFETY_ANALYSIS {
  xlog("thread 1 started\n");

  for (int times = 0; times < 300 || !got_lock_1; times++) {
    zx_status_t status = sync_mutex_trylock(&g_mutex);
    zx_nanosleep(zx_deadline_after(ZX_USEC(1)));
    if (status == ZX_OK) {
      got_lock_1 = true;
      sync_mutex_unlock(&g_mutex);
    }
  }

  xlog("thread 1 done\n");
  return 0;
}

static int mutex_try_thread_2(void* arg) TA_NO_THREAD_SAFETY_ANALYSIS {
  xlog("thread 2 started\n");

  for (int times = 0; times < 150 || !got_lock_2; times++) {
    zx_status_t status = sync_mutex_trylock(&g_mutex);
    zx_nanosleep(zx_deadline_after(ZX_USEC(2)));
    if (status == ZX_OK) {
      got_lock_2 = true;
      sync_mutex_unlock(&g_mutex);
    }
  }

  xlog("thread 2 done\n");
  return 0;
}

static int mutex_try_thread_3(void* arg) TA_NO_THREAD_SAFETY_ANALYSIS {
  xlog("thread 3 started\n");

  for (int times = 0; times < 100 || !got_lock_3; times++) {
    zx_status_t status = sync_mutex_trylock(&g_mutex);
    zx_nanosleep(zx_deadline_after(ZX_USEC(3)));
    if (status == ZX_OK) {
      got_lock_3 = true;
      sync_mutex_unlock(&g_mutex);
    }
  }

  xlog("thread 3 done\n");
  return 0;
}

TEST(SyncMutex, Mutexes) {
  thrd_t thread1, thread2, thread3;

  thrd_create_with_name(&thread1, mutex_thread_1, nullptr, "thread 1");
  thrd_create_with_name(&thread2, mutex_thread_2, nullptr, "thread 2");
  thrd_create_with_name(&thread3, mutex_thread_3, nullptr, "thread 3");

  thrd_join(thread1, nullptr);
  thrd_join(thread2, nullptr);
  thrd_join(thread3, nullptr);
}

TEST(SyncMutex, TryMutexes) {
  thrd_t thread1, thread2, thread3;

  thrd_create_with_name(&thread1, mutex_try_thread_1, nullptr, "thread 1");
  thrd_create_with_name(&thread2, mutex_try_thread_2, nullptr, "thread 2");
  thrd_create_with_name(&thread3, mutex_try_thread_3, nullptr, "thread 3");

  thrd_join(thread1, nullptr);
  thrd_join(thread2, nullptr);
  thrd_join(thread3, nullptr);

  EXPECT_TRUE(got_lock_1, "failed to get lock 1");
  EXPECT_TRUE(got_lock_2, "failed to get lock 2");
  EXPECT_TRUE(got_lock_3, "failed to get lock 3");
}

typedef struct {
  sync_mutex_t mutex;
  zx_handle_t start_event;
  zx_handle_t done_event;
} timeout_args;

static int test_timeout_helper(void* ctx) TA_NO_THREAD_SAFETY_ANALYSIS {
  timeout_args* args = reinterpret_cast<timeout_args*>(ctx);
  sync_mutex_lock(&args->mutex);
  // Inform the main thread that we have acquired the lock.
  ZX_ASSERT(zx_object_signal(args->start_event, 0, ZX_EVENT_SIGNALED) == ZX_OK);
  // Wait until the main thread has completed its test.
  ZX_ASSERT(zx_object_wait_one(args->done_event, ZX_EVENT_SIGNALED, ZX_TIME_INFINITE, nullptr) ==
            ZX_OK);
  sync_mutex_unlock(&args->mutex);
  return 0;
}

TEST(SyncMutex, TimeoutElapsed) {
  const zx_duration_t kRelativeDeadline = ZX_MSEC(100);

  timeout_args args;
  ASSERT_EQ(zx_event_create(0, &args.start_event), ZX_OK, "could not create event");
  ASSERT_EQ(zx_event_create(0, &args.done_event), ZX_OK, "could not create event");

  thrd_t helper;
  ASSERT_EQ(thrd_create(&helper, test_timeout_helper, &args), thrd_success, "");
  // Wait for the helper thread to acquire the lock.
  ASSERT_EQ(zx_object_wait_one(args.start_event, ZX_EVENT_SIGNALED, ZX_TIME_INFINITE, nullptr),
            ZX_OK, "failed to wait");

  for (int i = 0; i < 5; ++i) {
    zx_time_t now = zx_clock_get_monotonic();
    zx_status_t status = sync_mutex_timedlock(&args.mutex, now + kRelativeDeadline);
    ASSERT_EQ(status, ZX_ERR_TIMED_OUT, "wait should time out");
    zx_duration_t elapsed = zx_time_sub_time(zx_clock_get_monotonic(), now);
    EXPECT_GE(elapsed, kRelativeDeadline, "wait returned early");
  }

  // Inform the helper thread that we are done.
  ASSERT_EQ(zx_object_signal(args.done_event, 0, ZX_EVENT_SIGNALED), ZX_OK, "failed to signal");
  ASSERT_EQ(thrd_join(helper, nullptr), thrd_success, "failed to join");

  ASSERT_EQ(zx_handle_close(args.start_event), ZX_OK, "failed to close event");
  ASSERT_EQ(zx_handle_close(args.done_event), ZX_OK, "failed to close event");
}
