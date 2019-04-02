// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <fbl/auto_lock.h>
#include <io-scheduler/io-scheduler.h>
#include <lib/fzl/fifo.h>
#include <zxtest/zxtest.h>

namespace {

using IoScheduler = ioscheduler::Scheduler;
using SchedOp = ioscheduler::SchedulerOp;

constexpr uint32_t kMaxFifoDepth = (4096 / sizeof(void*));

class IOSchedTestFixture : public zxtest::Test, public ioscheduler::SchedulerClient {
public:
    IoScheduler* Scheduler() { return sched_.get(); }

protected:
    // Called before every test of this test case.
    void SetUp() override {
        ASSERT_EQ(sched_, nullptr);
        sched_.reset(new IoScheduler());
    }

    // Called after every test of this test case.
    void TearDown() override {
        sched_.release();
    }

    void CreateFifo() {
        ASSERT_OK(fzl::create_fifo(kMaxFifoDepth, 0, &fifo_to_server_, &fifo_from_server_),
                  "Failed to create FIFOs");
    }


    // Callback methods.
    bool CanReorder(SchedOp* first, SchedOp* second) override {
        return false;
    }

    zx_status_t Acquire(SchedOp** sop_list, size_t list_count,
                           size_t* actual_count, bool wait) override {
        return ZX_OK;
    }

    zx_status_t Issue(SchedOp* sop) override {
        return ZX_OK;
    }

    void Release(SchedOp* sop) override {}
    void CancelAcquire() override {}
    void Fatal() override {}

    std::unique_ptr<IoScheduler> sched_ = nullptr;
    fzl::fifo<void*, void*> fifo_to_server_;
    fzl::fifo<void*, void*> fifo_from_server_;

private:
};

// Create and destroy scheduler.
TEST_F(IOSchedTestFixture, CreateTest) {
    ASSERT_TRUE(true);
}

// Init scheduler.
TEST_F(IOSchedTestFixture, InitTest) {
    zx_status_t status = sched_->Init(this, ioscheduler::kOptionStrictlyOrdered);
    ASSERT_OK(status, "Failed to init scheduler");
    sched_->Shutdown();
}

// Open streams.
TEST_F(IOSchedTestFixture, OpenTest) {
    zx_status_t status = sched_->Init(this, ioscheduler::kOptionStrictlyOrdered);
    ASSERT_OK(status, "Failed to init scheduler");

    // Open streams.
    status = sched_->StreamOpen(5, ioscheduler::kDefaultPriority);
    ASSERT_OK(status, "Failed to open stream");
    status = sched_->StreamOpen(0, ioscheduler::kDefaultPriority);
    ASSERT_OK(status, "Failed to open stream");
    status = sched_->StreamOpen(5, ioscheduler::kDefaultPriority);
    ASSERT_NOT_OK(status, "Expected failure to open duplicate stream");
    status = sched_->StreamOpen(3, 100000);
    ASSERT_NOT_OK(status, "Expected failure to open with invalid priority");
    status = sched_->StreamOpen(3, 1);
    ASSERT_OK(status, "Failed to open stream");

    // Close streams.
    status = sched_->StreamClose(5);
    ASSERT_OK(status, "Failed to close stream");
    status = sched_->StreamClose(3);
    ASSERT_OK(status, "Failed to close stream");
    // Stream 0 intentionally left open here.

    sched_->Shutdown();
}

// Serve.
TEST_F(IOSchedTestFixture, ServeTest) {
    zx_status_t status = sched_->Init(this, ioscheduler::kOptionStrictlyOrdered);
    ASSERT_OK(status, "Failed to init scheduler");
    status = sched_->StreamOpen(0, ioscheduler::kDefaultPriority);
    ASSERT_OK(status, "Failed to open stream");

    ASSERT_NO_FATAL_FAILURES(CreateFifo(), "Internal test failure");
    ASSERT_OK(sched_->Serve(), "Failed to begin service");

    // TODO: insert ops into FIFO.

    status = sched_->StreamClose(0);
    ASSERT_OK(status, "Failed to close stream");
    sched_->Shutdown();
}

} // namespace

