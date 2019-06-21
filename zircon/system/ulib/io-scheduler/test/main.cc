// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <zxtest/zxtest.h>

#include <io-scheduler/io-scheduler.h>

namespace {

using IoScheduler = ioscheduler::Scheduler;
using SchedOp = ioscheduler::StreamOp;

enum {
    kStageInput = 0,
    kStageAcquired,
    kStageIssued,
    kStageCompleted,
    kStageReleased
};

// Wrapper around StreamOp.
class TestOp : public fbl::DoublyLinkedListable<fbl::RefPtr<TestOp>>,
               public fbl::RefCounted<TestOp> {
public:
    TestOp(uint32_t id, uint32_t stream_id, uint32_t group = ioscheduler::kOpGroupNone) : id_(id),
        sop_(ioscheduler::OpType::kOpTypeUnknown, stream_id, group, 0, this) {}

    void set_id(uint32_t id) { id_ = id; }
    uint32_t id() { return id_; }
    bool should_fail() { return should_fail_; }
    void set_should_fail(bool should_fail) { should_fail_ = should_fail; }
    void set_result(zx_status_t result) { sop_.set_result(result); }
    bool CheckExpected() {
        return (should_fail_ ? (sop_.result() != ZX_OK) : (sop_.result() == ZX_OK));
    }
    zx_status_t result() { return sop_.result(); }
    SchedOp* sop() { return &sop_; }
    bool async() { return async_; }

    uint32_t stage() { return stage_; }
    void set_stage(uint32_t stage) { stage_ = stage; }

private:
    uint32_t id_ = 0;
    bool async_ = false;        // Should the op be completed asynchronously.
    bool should_fail_ = false;  // Should Issue() return an error for this op.
    uint32_t stage_ = kStageInput;
    SchedOp sop_{};
};

using TopRef = fbl::RefPtr<TestOp>;

class IOSchedTestFixture : public zxtest::Test, public ioscheduler::SchedulerClient {
public:
    IoScheduler* Scheduler() { return sched_.get(); }

protected:
    // Called before every test of this test case.
    void SetUp() override {
        fbl::AutoLock lock(&lock_);
        ASSERT_EQ(sched_, nullptr);
        end_requested_ = false;
        end_of_stream_ = false;
        in_total_ = 0;
        acquired_total_ = 0;
        issued_total_ = 0;
        completed_total_ = 0;
        released_total_ = 0;
        sched_.reset(new IoScheduler());
    }

    // Called after every test of this test case.
    void TearDown() override {
        fbl::AutoLock lock(&lock_);
        in_list_.clear();
        acquired_list_.clear();
        issued_list_.clear();
        completed_list_.clear();
        released_list_.clear();
        sched_.reset();
    }

    void DoServeTest(uint32_t ops, bool async, uint32_t fail_random);

    void InsertOp(TopRef top);

    // Wait until all inserted ops have been acquired.
    void WaitAcquire();
    void CheckExpectedResult();

    // Callback methods.
    bool CanReorder(SchedOp* first, SchedOp* second) override {
        return false;
    }

    zx_status_t Acquire(SchedOp** sop_list, size_t list_count,
                           size_t* actual_count, bool wait) override;
    zx_status_t Issue(SchedOp* sop) override;
    void Release(SchedOp* sop) override;
    void CancelAcquire() override;
    void Fatal() override;

    std::unique_ptr<IoScheduler> sched_ = nullptr;

private:
    void EndStreamLocked() __TA_REQUIRES(lock_);

    fbl::Mutex lock_;
    bool end_requested_ __TA_GUARDED(lock_) = false;    // Request closing the stream.
    bool end_of_stream_ __TA_GUARDED(lock_) = false;    // Stream has been closed.

    // Fields to track the ops passing through the stages of the pipeline.

    // Number of ops inserted into test fixture.
    uint32_t in_total_ __TA_GUARDED(lock_) = 0;

    // Number of ops pulled by scheduler via Acquire callback.
    uint32_t acquired_total_ __TA_GUARDED(lock_) = 0;

    // Number of ops seen via the Issue callback.
    uint32_t issued_total_ __TA_GUARDED(lock_) = 0;

    // Number of ops whose status has been reported as completed, either synchronously through Issue
    // return or via an AsyncComplete call.
    uint32_t completed_total_ __TA_GUARDED(lock_) = 0;

    // Number of ops released via the Release callback.
    uint32_t released_total_ __TA_GUARDED(lock_) = 0;

    // Event signalling ops are available in in_list_.
    // Used by the acquire callback to block on input.
    fbl::ConditionVariable in_avail_ __TA_GUARDED(lock_);

    // Event signalling all pending ops have been acquired.  Used by test shutdown threads to drain
    // the input pipeline.
    fbl::ConditionVariable acquired_all_ __TA_GUARDED(lock_);

    // List of ops inserted by the test but not yet acquired.
    fbl::DoublyLinkedList<TopRef> in_list_ __TA_GUARDED(lock_);

    // List of ops acquired by the scheduler but not yet issued.
    fbl::DoublyLinkedList<TopRef> acquired_list_ __TA_GUARDED(lock_);

    // List of ops issued by the scheduler but not yet complete.
    fbl::DoublyLinkedList<TopRef> issued_list_ __TA_GUARDED(lock_);

    // List of ops completed by the scheduler but not yet released.
    fbl::DoublyLinkedList<TopRef> completed_list_ __TA_GUARDED(lock_);

    // List of ops released by the scheduler.
    fbl::DoublyLinkedList<TopRef> released_list_ __TA_GUARDED(lock_);
};

void IOSchedTestFixture::InsertOp(TopRef top) {
    fbl::AutoLock lock(&lock_);
    ZX_DEBUG_ASSERT(end_requested_ == false);
    bool was_empty = in_list_.is_empty();
    in_list_.push_back(std::move(top));
    in_total_++;
    if (was_empty) {
        in_avail_.Signal();
    }
}

void IOSchedTestFixture::EndStreamLocked() {
    // Request exit.
    end_requested_ = true;
    in_avail_.Signal();
}

void IOSchedTestFixture::WaitAcquire() {
    fbl::AutoLock lock(&lock_);
    EndStreamLocked();
    // Wait for acknowledgement and stream to be drained.
    while (!end_of_stream_ || !in_list_.is_empty()) {
        acquired_all_.Wait(&lock_);
    }
    ZX_DEBUG_ASSERT(in_list_.is_empty());
    ZX_DEBUG_ASSERT(in_total_ == acquired_total_);
}

void IOSchedTestFixture::CheckExpectedResult() {
    fbl::AutoLock lock(&lock_);
    ASSERT_EQ(in_total_, acquired_total_);
    ASSERT_EQ(in_total_, issued_total_);
    ASSERT_EQ(in_total_, completed_total_);
    ASSERT_EQ(in_total_, released_total_);
    ASSERT_TRUE(in_list_.is_empty());
    ASSERT_TRUE(acquired_list_.is_empty());
    ASSERT_TRUE(issued_list_.is_empty());
    ASSERT_TRUE(completed_list_.is_empty());
    for ( ; ; ) {
        TopRef top = released_list_.pop_front();
        if (top == nullptr) {
            break;
        }
        ASSERT_TRUE(top->CheckExpected());
    }
}

zx_status_t IOSchedTestFixture::Acquire(SchedOp** sop_list, size_t list_count,
                                        size_t* actual_count, bool wait) {
    fbl::AutoLock lock(&lock_);
    while (in_list_.is_empty()) {
        if (end_requested_) {
            end_of_stream_ = true;
            acquired_all_.Broadcast();
            return ZX_ERR_CANCELED;
        }
        if (!wait) {
            return ZX_ERR_SHOULD_WAIT;
        }
        in_avail_.Wait(&lock_);
    }

    size_t i = 0;
    for ( ; i < list_count; i++) {
        TopRef top = in_list_.pop_front();
        if (top == nullptr) {
            break;
        }
        top->set_stage(kStageAcquired);
        sop_list[i] = top->sop();
        acquired_list_.push_back(std::move(top));
    }
    acquired_total_ += static_cast<uint32_t>(i);
    *actual_count = i;
    return ZX_OK;
}

zx_status_t IOSchedTestFixture::Issue(SchedOp* sop) {
    fbl::AutoLock lock(&lock_);
    issued_total_++;
    TopRef top = acquired_list_.erase(*static_cast<TestOp*>(sop->cookie()));
    if (top->async()) {
        // Will be completed asynchronously.
        top->set_stage(kStageIssued);
        issued_list_.push_back(std::move(top));
        return ZX_ERR_ASYNC;
    }

    // Executing op here...
    // Todo: pretend to do work here.
    if (top->should_fail()) {
        top->set_result(ZX_ERR_BAD_PATH); // Error unlikely to be generated by IO Scheduler.
    } else {
        top->set_result(ZX_OK);
    }
    top->set_stage(kStageCompleted);
    completed_list_.push_back(std::move(top));
    completed_total_++;
    return ZX_OK;
}

void IOSchedTestFixture::Release(SchedOp* sop) {
    fbl::AutoLock lock(&lock_);
    TestOp* top = static_cast<TestOp*>(sop->cookie());
    TopRef ref;
    uint32_t stage = top->stage();
    switch (stage) {
    case kStageAcquired:
        ref = acquired_list_.erase(*top);
        break;
    case kStageIssued:
        ref = issued_list_.erase(*top);
        break;
    case kStageCompleted:
        ref = completed_list_.erase(*top);
        break;
    default:
        fprintf(stderr, "Invalid op stage %u\n", stage);
        ZX_DEBUG_ASSERT(false);
        return;
    }
    top->set_stage(kStageReleased);
    released_list_.push_back(std::move(ref));
    released_total_++;
}

void IOSchedTestFixture::CancelAcquire() {
    fbl::AutoLock lock(&lock_);
    if (!end_of_stream_) {
        EndStreamLocked();
    }
}

void IOSchedTestFixture::Fatal() {
    ZX_DEBUG_ASSERT(false);
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

void IOSchedTestFixture::DoServeTest(uint32_t num_ops, bool async, uint32_t fail_pct) {
    zx_status_t status = sched_->Init(this, ioscheduler::kOptionStrictlyOrdered);
    ASSERT_OK(status, "Failed to init scheduler");
    status = sched_->StreamOpen(0, ioscheduler::kDefaultPriority);
    ASSERT_OK(status, "Failed to open stream");

    for (uint32_t i = 0; i < num_ops; i++) {
        TopRef top = fbl::AdoptRef(new TestOp(i, 0));
        if (fail_pct) {
            if((static_cast<uint32_t>(rand()) % 100u) < fail_pct) {
                top->set_should_fail(true);
            }
        }
        InsertOp(std::move(top));
    }
    ASSERT_OK(sched_->Serve(), "Failed to begin service");

    // Wait until all ops have been acquired.
    WaitAcquire();

    ASSERT_OK(sched_->StreamClose(0), "Failed to close stream");
    sched_->Shutdown();

    // Assert all ops completed.
    CheckExpectedResult();
}

TEST_F(IOSchedTestFixture, ServeTestSingle) {
    DoServeTest(1, false, 0);
}

TEST_F(IOSchedTestFixture, ServeTestMulti) {
    DoServeTest(200, false, 0);
}

TEST_F(IOSchedTestFixture, ServeTestMultiFailures) {
    DoServeTest(200, false, 10);
}

TEST_F(IOSchedTestFixture, ServeTestMultistream) {
    zx_status_t status = sched_->Init(this, ioscheduler::kOptionStrictlyOrdered);
    ASSERT_OK(status, "Failed to init scheduler");
    const uint32_t num_streams = 5;
    for (uint32_t i = 0; i < num_streams; i++) {
        status = sched_->StreamOpen(i, ioscheduler::kDefaultPriority);
        ASSERT_OK(status, "Failed to open stream");
    }

    const uint32_t num_ops = num_streams * 1000;
    uint32_t op_id;
    // Add half of the ops before starting the server.
    for (op_id = 0; op_id < num_ops / 2; op_id++) {
        uint32_t stream_id = (static_cast<uint32_t>(rand()) % num_streams);
        TopRef top = fbl::AdoptRef(new TestOp(op_id, stream_id));
        InsertOp(std::move(top));
    }

    ASSERT_OK(sched_->Serve(), "Failed to begin service");

    // Add other half while running.
    for ( ; op_id < num_ops; op_id++) {
        uint32_t stream_id = (static_cast<uint32_t>(rand()) % num_streams);
        TopRef top = fbl::AdoptRef(new TestOp(op_id, stream_id));
        InsertOp(std::move(top));
    }

    // Wait until all ops have been acquired.
    WaitAcquire();

    ASSERT_OK(sched_->StreamClose(0), "Failed to close stream");
    // Other streams intentionally left open. Will be closed by Shutdown().
    sched_->Shutdown();

    // Assert all ops completed.
    CheckExpectedResult();
}

} // namespace

