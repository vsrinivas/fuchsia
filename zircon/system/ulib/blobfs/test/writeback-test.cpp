// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/writeback.h>
#include <zircon/assert.h>
#include <zxtest/zxtest.h>

#include "utils.h"

namespace blobfs {
namespace {

// Enqueue a request which fits within writeback buffer.
TEST(EnqueuePaginated, EnqueueSmallRequests) {
    MockTransactionManager transaction_manager;
    zx::vmo vmo;
    fbl::unique_ptr<WritebackWork> work;

    constexpr size_t kXferSize = kWritebackCapacity * kBlockSize;
    ASSERT_EQ(ZX_OK, zx::vmo::create(kXferSize, 0, &vmo));
    ASSERT_EQ(ZX_OK, transaction_manager.CreateWork(&work, nullptr));
    ASSERT_EQ(ZX_OK, EnqueuePaginated(&work, &transaction_manager, nullptr,
                                      vmo, 0, 0, kXferSize / kBlockSize));
    ASSERT_EQ(ZX_OK, transaction_manager.EnqueueWork(std::move(work), EnqueueType::kData));
}

// Enqueue a request which does not fit within writeback buffer.
TEST(EnqueuePaginated, EnqueueLargeRequests) {
    MockTransactionManager transaction_manager;
    zx::vmo vmo;
    fbl::unique_ptr<WritebackWork> work;

    constexpr size_t kXferSize = kWritebackCapacity * kBlockSize;
    ASSERT_EQ(ZX_OK, zx::vmo::create(kXferSize, 0, &vmo));
    ASSERT_EQ(ZX_OK, transaction_manager.CreateWork(&work, nullptr));
    ASSERT_EQ(ZX_OK, EnqueuePaginated(&work, &transaction_manager, nullptr,
                                      vmo, 0, 0, kXferSize / kBlockSize));
    ASSERT_EQ(ZX_OK, transaction_manager.EnqueueWork(std::move(work), EnqueueType::kData));
}

// Enqueue multiple requests at once, which combine to be larger than the writeback buffer.
TEST(EnqueuePaginatedTest, EnqueueMany) {
    MockTransactionManager transaction_manager;
    zx::vmo vmo;
    fbl::unique_ptr<WritebackWork> work;

    ASSERT_EQ(ZX_OK, zx::vmo::create(kWritebackCapacity * kBlockSize, 0, &vmo));
    ASSERT_EQ(ZX_OK, transaction_manager.CreateWork(&work, nullptr));

    constexpr size_t kSegments = 4;
    constexpr size_t kBufferSizeBytes = kWritebackCapacity * kBlockSize;
    static_assert(kBufferSizeBytes % 4 == 0, "Bad segment count");
    constexpr size_t kXferSize = kBufferSizeBytes / kSegments;
    for (size_t i = 0; i < kSegments; i++) {
        ASSERT_EQ(ZX_OK, EnqueuePaginated(&work, &transaction_manager, nullptr,
                                          vmo,
                                          (i * kXferSize) / kBlockSize,
                                          (i * kXferSize) / kBlockSize,
                                          kXferSize / kBlockSize));
    }
    ASSERT_EQ(ZX_OK, transaction_manager.EnqueueWork(std::move(work), EnqueueType::kData));
}

// Test that multiple completion callbacks may be added to a single WritebackWork.
TEST(WritebackWorkTest, WritebackWorkOrder) {
    MockTransactionManager transaction_manager;
    zx::vmo vmo;
    fbl::unique_ptr<WritebackWork> work;

    ASSERT_EQ(ZX_OK, zx::vmo::create(kBlockSize, 0, &vmo));
    ASSERT_EQ(ZX_OK, transaction_manager.CreateWork(&work, nullptr));

    bool alpha_completion_done = false;
    bool beta_completion_done = false;

    // Current implementation documents that enqueueing "A, B" results in the completion of "B, A".

    work->SetSyncCallback([&](zx_status_t status) {
        ZX_DEBUG_ASSERT_MSG(status == ZX_OK, "Unexpected callback status");
        ZX_DEBUG_ASSERT_MSG(!alpha_completion_done, "Repeated completion");
        ZX_DEBUG_ASSERT_MSG(beta_completion_done, "Bad completion order");
        alpha_completion_done = true;
    });

    work->SetSyncCallback([&](zx_status_t status) {
        ZX_DEBUG_ASSERT_MSG(status == ZX_OK, "Unexpected callback status");
        ZX_DEBUG_ASSERT_MSG(!alpha_completion_done, "Bad completion order");
        ZX_DEBUG_ASSERT_MSG(!beta_completion_done, "Repeated completion");
        beta_completion_done = true;
    });

    ASSERT_FALSE(alpha_completion_done);
    ASSERT_FALSE(beta_completion_done);

    work->MarkCompleted(ZX_OK);

    ASSERT_TRUE(alpha_completion_done);
    ASSERT_TRUE(beta_completion_done);
}

TEST(FlushRequestsTest, FlushNoRequests) {
    class TestTransactionManager : public MockTransactionManager {
        zx_status_t Transaction(block_fifo_request_t* requests, size_t count) final {
            ZX_ASSERT_MSG(false, "Zero requests should not invoke the Transaction operation");
        }
    } manager;
    fbl::Vector<BufferedOperation> operations;
    EXPECT_EQ(ZX_OK, FlushWriteRequests(&manager, operations));
}

TEST(FlushRequestsTest, FlushOneRequest) {
    static constexpr vmoid_t kVmoid = 4;
    class TestTransactionManager : public MockTransactionManager {
        zx_status_t Transaction(block_fifo_request_t* requests, size_t count) final {
            EXPECT_EQ(1, count);
            EXPECT_EQ(1 * kDiskBlockRatio, requests[0].vmo_offset);
            EXPECT_EQ(2 * kDiskBlockRatio, requests[0].dev_offset);
            EXPECT_EQ(3 * kDiskBlockRatio, requests[0].length);
            EXPECT_EQ(kVmoid, requests[0].vmoid);
            return ZX_OK;
        }
    } manager;
    fbl::Vector<BufferedOperation> operations;
    operations.push_back(BufferedOperation { kVmoid, Operation { OperationType::kWrite, 1, 2, 3 }});
    EXPECT_EQ(ZX_OK, FlushWriteRequests(&manager, operations));
}

TEST(FlushRequestsTest, FlushManyRequests) {
    static constexpr vmoid_t kVmoidA = 7;
    static constexpr vmoid_t kVmoidB = 8;
    class TestTransactionManager : public MockTransactionManager {
        zx_status_t Transaction(block_fifo_request_t* requests, size_t count) final {
            EXPECT_EQ(2, count);
            EXPECT_EQ(1 * kDiskBlockRatio, requests[0].vmo_offset);
            EXPECT_EQ(2 * kDiskBlockRatio, requests[0].dev_offset);
            EXPECT_EQ(3 * kDiskBlockRatio, requests[0].length);
            EXPECT_EQ(4 * kDiskBlockRatio, requests[1].vmo_offset);
            EXPECT_EQ(5 * kDiskBlockRatio, requests[1].dev_offset);
            EXPECT_EQ(6 * kDiskBlockRatio, requests[1].length);
            EXPECT_EQ(kVmoidA, requests[0].vmoid);
            EXPECT_EQ(kVmoidB, requests[1].vmoid);
            return ZX_OK;
        }
    } manager;
    fbl::Vector<BufferedOperation> operations;
    operations.push_back(BufferedOperation {
        kVmoidA,
        Operation { OperationType::kWrite, 1, 2, 3 }
    });
    operations.push_back(BufferedOperation {
        kVmoidB,
        Operation { OperationType::kWrite, 4, 5, 6 }
    });
    EXPECT_EQ(ZX_OK, FlushWriteRequests(&manager, operations));
}

TEST(FlushRequestsTest, BadFlush) {
    class TestTransactionManager : public MockTransactionManager {
        zx_status_t Transaction(block_fifo_request_t* requests, size_t count) final {
            return ZX_ERR_NOT_SUPPORTED;
        }
    } manager;
    fbl::Vector<BufferedOperation> operations;
    operations.push_back(BufferedOperation { 1, Operation { OperationType::kWrite, 1, 2, 3 }});
    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, FlushWriteRequests(&manager, operations));
}

TEST(WritebackQueueTest, DestroyWritebackWithoutTeardown) {
    MockTransactionManager transaction_manager;
    fbl::unique_ptr<WritebackQueue> writeback_;
    EXPECT_EQ(ZX_OK, WritebackQueue::Create(&transaction_manager, kWritebackCapacity, &writeback_));
    writeback_.reset();
}

} // namespace
} // namespace blobfs
