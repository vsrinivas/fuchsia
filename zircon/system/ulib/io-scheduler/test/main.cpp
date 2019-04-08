// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <fbl/auto_lock.h>
#include <lib/fzl/fifo.h>
#include <zircon/listnode.h>
#include <zxtest/zxtest.h>

#include <io-scheduler/io-scheduler.h>

namespace {

using IoScheduler = ioscheduler::Scheduler;
using SchedOp = ioscheduler::SchedulerOp;

constexpr uint32_t kMaxFifoDepth = (4096 / sizeof(void*));

// Wrapper around SchedulerOp.
struct TestOp {
    SchedOp sop;
    list_node_t node;
    uint32_t id;
    bool enqueued;
    bool issued;
    bool released;
};

class IOSchedTestFixture : public zxtest::Test, public ioscheduler::SchedulerClient {
public:
    IoScheduler* Scheduler() { return sched_.get(); }

protected:
    // Called before every test of this test case.
    void SetUp() override {
        ASSERT_EQ(sched_, nullptr);
        canceled_ = false;
        fifo_ready_ = false;
        sched_.reset(new IoScheduler());
    }

    // Called after every test of this test case.
    void TearDown() override {
        sched_.release();
        server_end_.reset();
        client_end_.reset();
    }

    void CreateFifo() {
        ASSERT_OK(fzl::create_fifo(kMaxFifoDepth, 0, &server_end_, &client_end_),
                  "Failed to create FIFOs");
        fbl::AutoLock lock(&fifo_lock_);
        fifo_ready_ = true;
    }

    // Callback methods.
    bool CanReorder(SchedOp* first, SchedOp* second) override {
        return false;
    }

    zx_status_t Acquire(SchedOp** sop_list, size_t list_count,
                           size_t* actual_count, bool wait) override;
    zx_status_t Issue(SchedOp* sop) override {
        return ZX_OK;
    }

    void Release(SchedOp* sop) override {}
    void CancelAcquire() override;
    void Fatal() override {}

    std::unique_ptr<IoScheduler> sched_ = nullptr;

private:
    fzl::fifo<TestOp*, TestOp*> server_end_; // FIFO side accessed by server.
    fzl::fifo<TestOp*, TestOp*> client_end_; // FIFO side accessed by client.
    fbl::Mutex fifo_lock_;
    bool fifo_ready_;
    bool canceled_;
};

zx_status_t IOSchedTestFixture::Acquire(SchedOp** sop_list, size_t list_count,
                                        size_t* actual_count, bool wait) {
    fbl::AutoLock lock(&fifo_lock_);
    if (!fifo_ready_) {
        // This should not happen. The test was not set up properly if this is encountered.
        fprintf(stderr, "Invalid test condition\n");
        return ZX_ERR_INTERNAL;
    }
    if (canceled_) {
        return ZX_ERR_CANCELED;
    }

    const size_t count = fbl::min(2ul, list_count);
    zx_status_t status;
    for ( ; ; ) {
        size_t actual = 0;
        TestOp* tops_in[count];
        status = server_end_.read(tops_in, count, &actual);
        if (status == ZX_OK) {
            // Successful read.
            size_t i = 0;
            for ( ; i < actual; i++) {
                TestOp* top = tops_in[i];
                if (top == nullptr) {
                    // Termination message received.
                    canceled_ = true;
                    break;
                }
                sop_list[i] = &top->sop;
            }
            if (i == 0) {
                // Read none but the termination message.
                return ZX_ERR_CANCELED;
            }
            *actual_count = i;
            return ZX_OK;
        }
        if (status == ZX_ERR_SHOULD_WAIT) {
            // Fifo empty.
            if (!wait) {
                return ZX_ERR_SHOULD_WAIT;
            }
            zx_signals_t pending;
            status = server_end_.wait_one(ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED,
                                          zx::time::infinite(), &pending);
            if (status != ZX_OK) {
                fprintf(stderr, "Unexpected FIFO wait error %d\n", status);
                break;
            }
            if (pending & ZX_FIFO_READABLE) {
                // Drain the FIFO, even if peer closed it.
                continue;
            }
            // (pending & ZX_FIFO_PEER_CLOSED) - Peer exited without clean termination signal.
            status = ZX_ERR_CANCELED;
            break;
        }
        if (status == ZX_ERR_PEER_CLOSED) {
            // Peer exited without clean termination signal.
            status = ZX_ERR_CANCELED;
            break;
        }
        // Bad status.
        fprintf(stderr, "Unexpected FIFO read error %d\n", status);
        break;
    }
    canceled_ = true;
    return status;
}

void IOSchedTestFixture::CancelAcquire() {
    {
        fbl::AutoLock lock(&fifo_lock_);
        if (!fifo_ready_) {
            return;
        }
    }
    TestOp* top = nullptr;
    zx_status_t status = client_end_.write(&top, 1, nullptr);
    ASSERT_OK(status);
}

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

