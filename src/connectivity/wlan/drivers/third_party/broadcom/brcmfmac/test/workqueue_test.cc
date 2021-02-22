/*
 * Copyright (c) 2018 The Fuchsia Authors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/workqueue.h"

#include <lib/sync/completion.h>

#include <gtest/gtest.h>

namespace {

class CompletionSignal {
public:
  void Signal() {
    signaled_ = true;
    sync_completion_signal(&completion_);
  }

  void Wait(zx_duration_t timeout) {
    while (!signaled_) {
      sync_completion_wait(&completion_, timeout);
    }
  }

private:
  std::atomic<bool> signaled_ = false;
  sync_completion_t completion_;
};

struct TestWork {
  CompletionSignal entered;
  CompletionSignal proceed;
  WorkItem work;
  int state = 0;
};

static void handler(WorkItem* work) {
  TestWork* tester = containerof(work, TestWork, work);
  tester->state++;
  tester->entered.Signal();
  tester->proceed.Wait(ZX_TIME_INFINITE);
  tester->state++;
}

TEST(Workqueue, JobsInOrder) {
  WorkQueue* queue = new WorkQueue("MyWork");
  TestWork work1;
  TestWork work2;
  work1.work = WorkItem(handler);
  work2.work = WorkItem(handler);
  queue->Schedule(&work1.work);
  queue->Schedule(&work2.work);
  work1.entered.Wait(ZX_TIME_INFINITE);
  EXPECT_EQ(work1.state, 1);
  EXPECT_EQ(work2.state, 0);
  work1.proceed.Signal();
  work2.entered.Wait(ZX_TIME_INFINITE);
  EXPECT_EQ(work1.state, 2);
  EXPECT_EQ(work2.state, 1);
  work2.proceed.Signal();
  delete queue;
}

TEST(Workqueue, ScheduleDeduplication) {
  WorkQueue* queue = new WorkQueue("MyWork");
  TestWork work1;
  TestWork work2;
  work1.work = WorkItem(handler);
  work2.work = WorkItem(handler);
  // Queue up the first work item and wait for it to start
  queue->Schedule(&work1.work);
  work1.entered.Wait(ZX_TIME_INFINITE);
  // Then before it's allowed to proceed queue it again, this is allowed
  queue->Schedule(&work1.work);
  // Before the first work item completes queue up the second work item twice.
  // Because it's not running it should be deduplicated.
  queue->Schedule(&work2.work);
  queue->Schedule(&work2.work);
  // Allow both work items to proceed.
  work1.proceed.Signal();
  work2.proceed.Signal();
  // Destroy the queue to wait for all work items to complete
  delete queue;
  // The first work item should have run twice, the second only once.
  EXPECT_EQ(work1.state, 4);
  EXPECT_EQ(work2.state, 2);
}

TEST(Workqueue, DefaultQueueWorks) {
  TestWork work1;
  work1.work = WorkItem(handler);
  WorkQueue::ScheduleDefault(&work1.work);
  work1.entered.Wait(ZX_TIME_INFINITE);
  EXPECT_EQ(work1.state, 1);
  work1.proceed.Signal();
  WorkQueue::FlushDefault();
}

// Ensure that canceling an unqueued job works.
TEST(Workqueue, CancelUnqueued) {
  TestWork work1;
  work1.work = WorkItem(handler);
  work1.state = 1;
  work1.work.Cancel();
  EXPECT_EQ(work1.state, 1);
}

// Upon canceling a job that has not started yet, it should never run, and the cancel should
// return without blocking.
TEST(Workqueue, CancelPending) {
  WorkQueue* queue = new WorkQueue("MyWork");
  TestWork work1;
  TestWork work2;
  TestWork work3;

  work1.work = WorkItem(handler);
  work2.work = WorkItem(handler);
  work3.work = WorkItem(handler);

  queue->Schedule(&work1.work);
  queue->Schedule(&work2.work);
  queue->Schedule(&work3.work);

  work1.entered.Wait(ZX_TIME_INFINITE);
  EXPECT_EQ(work1.state, 1);
  EXPECT_EQ(work2.state, 0);
  work2.work.Cancel();
  work1.proceed.Signal();
  work3.entered.Wait(ZX_TIME_INFINITE);
  EXPECT_EQ(work1.state, 2);
  EXPECT_EQ(work2.state, 0);
  EXPECT_EQ(work3.state, 1);
  work3.proceed.Signal();
  delete queue;
}

struct WorkCanceler {
  WorkItem work;
  TestWork* target;
  CompletionSignal leaving;
  int state = 0;
};

static void cancel_handler(WorkItem* work) {
  WorkCanceler* canceler = containerof(work, WorkCanceler, work);
  canceler->target->work.Cancel();
  canceler->state = 2;
  canceler->leaving.Signal();
}

// Upon canceling a job that is in progress, the canceler should block until the job completes.
// Subsequent jobs should also complete.
TEST(Workqueue, CancelCurrent) {
  WorkQueue* queue = new WorkQueue("MyWork");
  TestWork work1;
  TestWork work2;
  WorkCanceler canceler;
  canceler.target = &work1;
  work1.work = WorkItem(handler);
  work2.work = WorkItem(handler);
  canceler.work = WorkItem(cancel_handler);
  queue->Schedule(&work1.work);
  queue->Schedule(&work2.work);
  work1.entered.Wait(ZX_TIME_INFINITE);
  EXPECT_EQ(work1.state, 1);
  EXPECT_EQ(work2.state, 0);
  WorkQueue::ScheduleDefault(&canceler.work);
  // If this timeout is too short and the canceler hasn't entered yet, it may cause a false pass,
  // but not a flaky fail.
  zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));
  EXPECT_EQ(work1.state, 1);
  EXPECT_EQ(canceler.state, 0);
  work1.proceed.Signal();
  work2.entered.Wait(ZX_TIME_INFINITE);
  EXPECT_EQ(work1.state, 2);
  EXPECT_EQ(work2.state, 1);
  canceler.leaving.Wait(ZX_TIME_INFINITE);
  work2.proceed.Signal();
  delete queue;
  // Make sure that the canceler finishes before we exit, canceling it if needed.
  canceler.work.Cancel();
}

}  // namespace
