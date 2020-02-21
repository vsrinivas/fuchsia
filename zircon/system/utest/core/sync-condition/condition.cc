// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sync/condition.h>
#include <sched.h>
#include <threads.h>
#include <zircon/syscalls.h>

#include <zxtest/zxtest.h>

namespace {

struct Mutex {
  sync_mutex_t mtx;

  void lock() __TA_ACQUIRE(mtx) { sync_mutex_lock(&mtx); }

  void unlock() __TA_RELEASE(mtx) { sync_mutex_unlock(&mtx); }
};

struct Condition {
  sync_condition_t condition;

  void signal() { sync_condition_signal(&condition); }

  void broadcast() { sync_condition_broadcast(&condition); }

  void wait(Mutex* mtx) { sync_condition_wait(&condition, &mtx->mtx); }

  zx_status_t timedwait(Mutex* mtx, zx_duration_t timeout) {
    return sync_condition_timedwait(&condition, &mtx->mtx, zx_deadline_after(timeout));
  }
};

struct Context {
  Mutex mutex;
  Condition cond;
  int threads_waked = 0;
  int threads_started = 0;
  int threads_woke_first_barrier = 0;
};

int cond_thread(void* arg) {
  auto* ctx = static_cast<Context*>(arg);

  ctx->mutex.lock();
  ctx->threads_started++;
  ctx->cond.wait(&ctx->mutex);
  ctx->threads_woke_first_barrier++;
  ctx->cond.wait(&ctx->mutex);
  ctx->threads_waked++;
  ctx->mutex.unlock();
  return 0;
}

TEST(SyncCondition, ConditionTest) {
  Context ctx;

  thrd_t thread1, thread2, thread3;

  thrd_create(&thread1, cond_thread, &ctx);
  thrd_create(&thread2, cond_thread, &ctx);
  thrd_create(&thread3, cond_thread, &ctx);

  // Wait for all the threads to report that they've started.
  while (true) {
    ctx.mutex.lock();
    int threads = ctx.threads_started;
    ctx.mutex.unlock();
    if (threads == 3) {
      break;
    }
    sched_yield();
  }

  ctx.cond.broadcast();

  // Wait for all the threads to report that they were woken.
  while (true) {
    ctx.mutex.lock();
    int threads = ctx.threads_woke_first_barrier;
    ctx.mutex.unlock();
    if (threads == 3) {
      break;
    }
    sched_yield();
  }

  for (int iteration = 0; iteration < 3; iteration++) {
    ctx.cond.signal();

    // Wait for one thread to report that it was woken.
    while (true) {
      ctx.mutex.lock();
      int threads = ctx.threads_waked;
      ctx.mutex.unlock();
      if (threads == iteration + 1) {
        break;
      }
      sched_yield();
    }
  }

  thrd_join(thread1, nullptr);
  thrd_join(thread2, nullptr);
  thrd_join(thread3, nullptr);
}

TEST(SyncCondition, TimeoutTest) {
  Condition cond;
  Mutex mutex;

  mutex.lock();
  zx_status_t result = cond.timedwait(&mutex, ZX_MSEC(1));
  mutex.unlock();

  EXPECT_EQ(result, ZX_ERR_TIMED_OUT, "Lock should have timeout");
}

}  // namespace
