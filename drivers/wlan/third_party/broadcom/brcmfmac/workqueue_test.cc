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

extern "C" {
#include "workqueue.h"

static bool error_happened;

void __brcmf_err(const char* func, const char* fmt, ...) {
    error_happened = true;
}

void __brcmf_dbg(uint32_t filter, const char* func, const char* fmt, ...) {}

} // extern "C"

#include <lib/sync/completion.h>

#include "gtest/gtest.h"

namespace {

struct TestWork {
    sync_completion_t entered;
    sync_completion_t leaving;
    sync_completion_t proceed;
    struct work_struct work;
    int state;
    TestWork();
};

TestWork::TestWork() :
    state(0) {
}

static void handler(struct work_struct* work) {
    TestWork* tester = containerof(work, TestWork, work);
    tester->state++;
    sync_completion_signal(&tester->entered);
    sync_completion_wait(&tester->proceed, ZX_TIME_INFINITE);
    tester->state++;
    sync_completion_signal(&tester->leaving);
}

TEST(Workqueue, JobsInOrder) {
    struct workqueue_struct* queue = workqueue_create("MyWork");
    TestWork work1;
    TestWork work2;
    workqueue_init_work(&work1.work, handler);
    workqueue_init_work(&work2.work, handler);
    workqueue_schedule(queue, &work1.work);
    workqueue_schedule(queue, &work2.work);
    sync_completion_wait(&work1.entered, ZX_TIME_INFINITE);
    EXPECT_EQ(work1.state, 1);
    EXPECT_EQ(work2.state, 0);
    sync_completion_signal(&work1.proceed);
    sync_completion_wait(&work2.entered, ZX_TIME_INFINITE);
    EXPECT_EQ(work1.state, 2);
    EXPECT_EQ(work2.state, 1);
    sync_completion_signal(&work2.proceed);
    workqueue_destroy(queue);
}

TEST(Workqueue, ScheduleTwiceIgnored) {
    struct workqueue_struct* queue = workqueue_create("MyWork");
    TestWork work1;
    TestWork work2;
    workqueue_init_work(&work1.work, handler);
    workqueue_init_work(&work2.work, handler);
    // Test for both current and pending work
    workqueue_schedule(queue, &work1.work);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));
    workqueue_schedule(queue, &work1.work);
    workqueue_schedule(queue, &work2.work);
    workqueue_schedule(queue, &work2.work);
    sync_completion_signal(&work1.proceed);
    sync_completion_signal(&work2.proceed);
    workqueue_destroy(queue);
    EXPECT_EQ(work1.state, 2);
    EXPECT_EQ(work2.state, 2);
}

TEST(Workqueue, DefaultQueueWorks) {
    TestWork work1;
    workqueue_init_work(&work1.work, handler);
    workqueue_schedule_default(&work1.work);
    sync_completion_wait(&work1.entered, ZX_TIME_INFINITE);
    EXPECT_EQ(work1.state, 1);
    sync_completion_signal(&work1.proceed);
    workqueue_flush_default();
}

// Upon canceling a job that has not started yet, it should never run, and the cancel should
// return without blocking.
TEST(Workqueue, CancelPending) {
    struct workqueue_struct* queue = workqueue_create("MyWork");
    TestWork work1;
    TestWork work2;
    TestWork work3;
    workqueue_init_work(&work1.work, handler);
    workqueue_init_work(&work2.work, handler);
    workqueue_init_work(&work3.work, handler);
    workqueue_schedule(queue, &work1.work);
    workqueue_schedule(queue, &work2.work);
    workqueue_schedule(queue, &work3.work);
    sync_completion_wait(&work1.entered, ZX_TIME_INFINITE);
    EXPECT_EQ(work1.state, 1);
    EXPECT_EQ(work2.state, 0);
    workqueue_cancel_work(&work2.work);
    sync_completion_signal(&work1.proceed);
    sync_completion_wait(&work3.entered, ZX_TIME_INFINITE);
    EXPECT_EQ(work1.state, 2);
    EXPECT_EQ(work2.state, 0);
    EXPECT_EQ(work3.state, 1);
    sync_completion_signal(&work3.proceed);
    workqueue_destroy(queue);
}

struct WorkCanceler {
    struct work_struct work;
    TestWork* target;
    sync_completion_t leaving;
    int state;
    WorkCanceler();
};

WorkCanceler::WorkCanceler() :
    state(0) {
}

static void cancel_handler(struct work_struct* work) {
    WorkCanceler* canceler = containerof(work, WorkCanceler, work);
    workqueue_cancel_work(&canceler->target->work);
    canceler->state = 2;
    sync_completion_signal(&canceler->leaving);
}

// Upon canceling a job that is in progress, the canceler should block until the job completes.
// Subsequent jobs should also complete.
TEST(Workqueue, CancelCurrent) {
    struct workqueue_struct* queue = workqueue_create("MyWork");
    TestWork work1;
    TestWork work2;
    WorkCanceler canceler;
    canceler.target = &work1;
    workqueue_init_work(&work1.work, handler);
    workqueue_init_work(&work2.work, handler);
    workqueue_init_work(&canceler.work, cancel_handler);
    workqueue_schedule(queue, &work1.work);
    workqueue_schedule(queue, &work2.work);
    sync_completion_wait(&work1.entered, ZX_TIME_INFINITE);
    EXPECT_EQ(work1.state, 1);
    EXPECT_EQ(work2.state, 0);
    workqueue_schedule_default(&canceler.work);
    // If this timeout is too short and the canceler hasn't entered yet, it may cause a false pass,
    // but not a flaky fail.
    zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));
    EXPECT_EQ(work1.state, 1);
    EXPECT_EQ(canceler.state, 0);
    sync_completion_signal(&work1.proceed);
    sync_completion_wait(&work2.entered, ZX_TIME_INFINITE);
    EXPECT_EQ(work1.state, 2);
    EXPECT_EQ(work2.state, 1);
    sync_completion_wait(&canceler.leaving, ZX_TIME_INFINITE);
    sync_completion_signal(&work2.proceed);
    workqueue_destroy(queue);
}

}  // namespace
