// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests DataBlockAssigner behavior.

#include <minfs/writeback.h>
#include <zxtest/zxtest.h>

#include "minfs-private.h"

namespace minfs {
namespace {

// Mock Minfs class to be used in DataBlockAssigner tests.
class MockMinfs : public TransactionalFs {
public:
    MockMinfs() = default;
    fbl::Mutex* GetLock() const { return &txn_lock_; }

    zx_status_t BeginTransaction(size_t reserve_inodes, size_t reserve_blocks,
                                 fbl::unique_ptr<Transaction>* out) {
        BlockIfPaused();
        ZX_ASSERT(reserve_inodes == 0);
        ZX_ASSERT(reserve_blocks == 0);
        return Transaction::Create(this, reserve_inodes, reserve_blocks, nullptr,
                                   nullptr, out);
    }

    zx_status_t CommitTransaction(fbl::unique_ptr<Transaction> state) {
        BlockIfPaused();
        ZX_ASSERT(state != nullptr);
        state->GetWork()->MarkCompleted(ZX_OK);
        return ZX_OK;
    }

    Bcache* GetMutableBcache() { return nullptr; }

    // Blocks any thread calling into the TransactionalFs interface.
    zx_status_t Pause() {
        fbl::AutoLock lock(&pause_lock_);
        if (paused_) {
            return ZX_ERR_BAD_STATE;
        }

        paused_ = true;
        return ZX_OK;
    }

    // Unblocks any thread calling into the TransactionalFs interface.
    zx_status_t Unpause() {
        fbl::AutoLock lock(&pause_lock_);
        if (!paused_) {
            return ZX_ERR_BAD_STATE;
        }

        paused_ = false;
        pause_cvar_.Signal();
        return ZX_OK;
    }

private:
    // Blocks until Minfs becomes "unpaused".
    void BlockIfPaused() {
        fbl::AutoLock lock(&pause_lock_);
        if (paused_) {
            pause_cvar_.Wait(&pause_lock_);
        }
    }

    mutable fbl::Mutex txn_lock_;

    // Variables used for pausing and unpausing Minfs' transactional interface.
    fbl::Mutex pause_lock_;
    bool paused_ __TA_GUARDED(pause_lock_) = false;
    fbl::ConditionVariable pause_cvar_;
};

// Mock Vnode class to be used in DataBlockAssigner tests.
class MockVnodeMinfs : public DataAssignableVnode, public fbl::Recyclable<MockVnodeMinfs> {
public:
    MockVnodeMinfs() = default;
    ~MockVnodeMinfs() = default;

    void fbl_recycle() final {
        if (recycled_ != nullptr) {
            *recycled_ = true;
        }
    }

    void SetRecycled(bool* recycled) {
        recycled_ = recycled;
        *recycled_ = false;
    }

    void AllocateData(Transaction* transaction) final {
        reserved_ = 0;
    }

    void Reserve(blk_t count) {
        reserved_ += count;
    }

    blk_t GetReserved() const { return reserved_; }

    bool IsDirectory() const { return false; }

private:
    blk_t reserved_ = 0;
    bool* recycled_;
};

class DataAssignerTest {
public:
    // Creates a new DataAssignerTest with valid MockMinfs and DataBlockAssigner.
    static zx_status_t Create(fbl::unique_ptr<DataAssignerTest>* out) {
        fbl::unique_ptr<DataAssignerTest> test(new DataAssignerTest());
        zx_status_t status = DataBlockAssigner::Create(&test->minfs_, &test->assigner_);
        if (status != ZX_OK) { return status;}
        *out = std::move(test);
        return ZX_OK;
    }

    ~DataAssignerTest() {
        Teardown();
    }

    void Teardown() {
        Unpause();
        assigner_.reset();
    }

    // Generates a new Vnode with |reserve_count| blocks reserved.
    void GenerateVnode(uint32_t reserve_count, fbl::RefPtr<MockVnodeMinfs>* out) {
        fbl::RefPtr<MockVnodeMinfs> mock_vnode = fbl::AdoptRef(new MockVnodeMinfs());
        ASSERT_NO_FATAL_FAILURES(mock_vnode->Reserve(reserve_count));
        *out = std::move(mock_vnode);
    }

    void EnqueueAllocation(fbl::RefPtr<MockVnodeMinfs> vnode) {
        assigner_->EnqueueAllocation(std::move(vnode));
    }

    void EnqueueCallback(SyncCallback callback) {
        assigner_->EnqueueCallback(std::move(callback));
    }

    zx_status_t Pause() {
        return minfs_.Pause();
    }

    zx_status_t Unpause() {
        return minfs_.Unpause();
    }

    // Blocks until waiting tasks are detected in assigner_. Returns true if waiting tasks were
    // found before the wait timed out.
    bool BlockUntilWaiting() {
        constexpr uint32_t timeout = 1000000;
        constexpr uint32_t increment = 1000;
        uint32_t total = 0;
        while (!assigner_->TasksWaiting() && total < timeout) {
            usleep(increment);
            total += increment;
        }

        return assigner_->TasksWaiting();
    }

    // Forcibly syncs the assigner_.
    zx_status_t Sync() {
        fbl::Mutex mutex;
        fbl::ConditionVariable cvar;
        fbl::AutoLock lock(&mutex);

        zx_status_t result;
        SyncCallback callback = [&mutex, &cvar, &result](zx_status_t status){
            fbl::AutoLock lock(&mutex);
            cvar.Signal();
            result = status;
        };

        assigner_->EnqueueCallback(std::move(callback));
        cvar.Wait(&mutex);
        return result;
    }

private:
    DataAssignerTest() {}

    MockMinfs minfs_;
    fbl::unique_ptr<DataBlockAssigner> assigner_;
};

TEST(DataAssignerTest, CheckVnodeRecycled) {
    fbl::RefPtr<MockVnodeMinfs> mock_vnode = fbl::AdoptRef(new MockVnodeMinfs());
    fbl::RefPtr<DataAssignableVnode> data_vnode = fbl::WrapRefPtr(mock_vnode.get());
    bool recycled;
    mock_vnode->SetRecycled(&recycled);
    ASSERT_FALSE(recycled);
    mock_vnode.reset();
    ASSERT_FALSE(recycled);
    data_vnode.reset();
    ASSERT_TRUE(recycled);
}

// Simple test which enqueues and processes a data block allocation for a single vnode.
TEST(DataAssignerTest, ProcessSingleNode) {
    fbl::unique_ptr<DataAssignerTest> test;
    ASSERT_OK(DataAssignerTest::Create(&test));
    fbl::RefPtr<MockVnodeMinfs> mock_vnode;
    ASSERT_NO_FATAL_FAILURES(test->GenerateVnode(10, &mock_vnode));
    ASSERT_EQ(10, mock_vnode->GetReserved());
    test->EnqueueAllocation(fbl::WrapRefPtr(mock_vnode.get()));
    ASSERT_OK(test->Sync());
    ASSERT_EQ(0, mock_vnode->GetReserved());
}

// Enqueue many data block allocation tasks.
TEST(DataAssignerTest, EnqueueMany) {
    fbl::unique_ptr<DataAssignerTest> test;
    ASSERT_OK(DataAssignerTest::Create(&test));
    fbl::RefPtr<MockVnodeMinfs> mock_vnode[kMaxQueued];

    for (unsigned i = 0; i < kMaxQueued; i++) {
        ASSERT_NO_FATAL_FAILURES(test->GenerateVnode(kMaxQueued * i, &mock_vnode[i]));
        test->EnqueueAllocation(fbl::WrapRefPtr(mock_vnode[i].get()));
    }

    ASSERT_OK(test->Sync());

    for (unsigned i = 0; i < kMaxQueued; i++) {
        ASSERT_EQ(0, mock_vnode[i]->GetReserved());
    }
}

// Try enqueueing an allocation when the assigner is already at capacity.
TEST(DataAssignerTest, EnqueueFull) {
    fbl::unique_ptr<DataAssignerTest> test;
    ASSERT_OK(DataAssignerTest::Create(&test));
    fbl::RefPtr<MockVnodeMinfs> mock_vnode[kMaxQueued];

    ASSERT_OK(test->Pause());

    for (unsigned i = 0; i < kMaxQueued; i++) {
        ASSERT_NO_FATAL_FAILURES(test->GenerateVnode(kMaxQueued * i, &mock_vnode[i]));
        test->EnqueueAllocation(fbl::WrapRefPtr(mock_vnode[i].get()));
    }

    auto process_tasks = [](void* arg) {
        DataAssignerTest* test = static_cast<DataAssignerTest*>(arg);
        if (!test->BlockUntilWaiting()) { return -1; }
        if (test->Unpause() != ZX_OK) { return -1; }
        return 0;
    };

    thrd_t process_thread;
    thrd_create(&process_thread, process_tasks, test.get());

    // The assigner queue is full, but attempt to enqueue a new allocation anyway. This will block
    // until the process_thread frees up space within the assigner.
    fbl::RefPtr<MockVnodeMinfs> another_vnode;
    ASSERT_NO_FATAL_FAILURES(test->GenerateVnode(1, &another_vnode));
    test->EnqueueAllocation(std::move(another_vnode));
    int result;
    ASSERT_EQ(thrd_join(process_thread, &result), thrd_success);
    ASSERT_EQ(result, 0);

    ASSERT_OK(test->Sync());

    for (unsigned i = 0; i < kMaxQueued; i++) {
        ASSERT_EQ(0, mock_vnode[i]->GetReserved());
    }
}

// Test enqueueing a callback.
TEST(DataAssignerTest, EnqueueCallback) {
    fbl::unique_ptr<DataAssignerTest> test;
    ASSERT_OK(DataAssignerTest::Create(&test));
    zx_status_t result = ZX_ERR_INVALID_ARGS;
    SyncCallback callback = [&](zx_status_t status){ result = status; };
    test->EnqueueCallback(std::move(callback));
    ASSERT_OK(test->Sync());
    ASSERT_OK(result);
}

// Go through processing steps until the assigner is in a waiting state, then enqueue an allocation
// job to wake it up.
TEST(DataAssignerTest, EnqueueWait) {
    fbl::unique_ptr<DataAssignerTest> test;
    ASSERT_OK(DataAssignerTest::Create(&test));

    // Sync the assigner to ensure we complete the processing step and are now waiting for more
    // tasks to be enqueued.
    ASSERT_OK(test->Sync());

    fbl::RefPtr<MockVnodeMinfs> mock_vnode;
    ASSERT_NO_FATAL_FAILURES(test->GenerateVnode(10, &mock_vnode));
    test->EnqueueAllocation(fbl::WrapRefPtr(mock_vnode.get()));

    ASSERT_OK(test->Sync());
    ASSERT_EQ(0, mock_vnode->GetReserved());
}

// Test that enqueued tasks which have not been processed are resolved on destruction.
TEST(DataAssignerTest, DestructAssigner) {
    fbl::unique_ptr<DataAssignerTest> test;
    ASSERT_OK(DataAssignerTest::Create(&test));
    fbl::RefPtr<MockVnodeMinfs> mock_vnode[kMaxQueued];

    for (unsigned i = 0; i < kMaxQueued; i++) {
        ASSERT_NO_FATAL_FAILURES(test->GenerateVnode(kMaxQueued * i, &mock_vnode[i]));
        test->EnqueueAllocation(fbl::WrapRefPtr(mock_vnode[i].get()));
    }

    test->Teardown();

    for (unsigned i = 0; i < kMaxQueued; i++) {
        ASSERT_EQ(0, mock_vnode[i]->GetReserved());
    }
}

// After enqueueing a vnode but before the assigner processes, destruct the original copy.
TEST(DataAssignerTest, DestructVnode) {
    fbl::unique_ptr<DataAssignerTest> test;
    ASSERT_OK(DataAssignerTest::Create(&test));
    fbl::RefPtr<MockVnodeMinfs> mock_vnode;
    ASSERT_NO_FATAL_FAILURES(test->GenerateVnode(1, &mock_vnode));
    test->EnqueueAllocation(fbl::WrapRefPtr(mock_vnode.get()));
    mock_vnode.reset();
    ASSERT_OK(test->Sync());
}

} // namespace
} // namespace minfs
