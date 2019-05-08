// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/writeback.h>
#include <blobfs/writeback-queue.h>
#include <zircon/assert.h>
#include <zxtest/zxtest.h>

namespace blobfs {
namespace {

constexpr uint32_t kBlockSize = 8192;
constexpr groupid_t kGroupID = 2;
constexpr uint32_t kDeviceBlockSize = 1024;
constexpr size_t kCapacity = 8;

class MockTransactionManager : public TransactionManager {
public:
    ~MockTransactionManager() {
        if (writeback_) {
            writeback_->Teardown();
        }
    }

    MockTransactionManager() {
        EXPECT_EQ(ZX_OK, WritebackQueue::Create(this, kCapacity, &writeback_));
    }

    uint32_t FsBlockSize() const final {
        return kBlockSize;
    }

    groupid_t BlockGroupID() final {
        return kGroupID;
    }

    uint32_t DeviceBlockSize() const final {
        return kDeviceBlockSize;
    }

    zx_status_t Transaction(block_fifo_request_t* requests, size_t count) override {
        // TODO(smklein): Improve validation.
        return ZX_OK;
    }

    const Superblock& Info() const final {
        return superblock_;
    }

    zx_status_t AddInodes(fzl::ResizeableVmoMapper* node_map) final {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t AddBlocks(size_t nblocks, RawBitmap* map) final {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t AttachVmo(const zx::vmo& vmo, vmoid_t* out) final {
        *out = 2;
        return ZX_OK;
    }

    zx_status_t DetachVmo(vmoid_t vmoid) final {
        return ZX_OK;
    }

    BlobfsMetrics& LocalMetrics() final {
        return metrics_;
    }

    size_t WritebackCapacity() const final {
        return kCapacity;
    }

    zx_status_t CreateWork(fbl::unique_ptr<WritebackWork>* out, Blob* vnode) final {
        ZX_ASSERT(out != nullptr);
        ZX_ASSERT(vnode == nullptr);

        out->reset(new WritebackWork(this));
        return ZX_OK;
    }

    zx_status_t EnqueueWork(fbl::unique_ptr<WritebackWork> work, EnqueueType type) final {
        ZX_ASSERT(type == EnqueueType::kData);
        return writeback_->Enqueue(std::move(work));
    }

private:
    fbl::unique_ptr<WritebackQueue> writeback_{};
    BlobfsMetrics metrics_{};
    Superblock superblock_{};
};

// Enqueue a request which fits within writeback buffer.
TEST(EnqueuePaginated, EnqueueSmallRequests) {
    MockTransactionManager transaction_manager;
    zx::vmo vmo;
    fbl::unique_ptr<WritebackWork> work;

    constexpr size_t kXferSize = kCapacity * kBlockSize;
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

    constexpr size_t kXferSize = kCapacity * kBlockSize;
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

    ASSERT_EQ(ZX_OK, zx::vmo::create(kCapacity * kBlockSize, 0, &vmo));
    ASSERT_EQ(ZX_OK, transaction_manager.CreateWork(&work, nullptr));

    constexpr size_t kSegments = 4;
    constexpr size_t kBufferSizeBytes = kCapacity * kBlockSize;
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
    fbl::Vector<Operation> operations;
    EXPECT_EQ(ZX_OK, FlushWriteRequests(&manager, operations, 2));
}

constexpr uint32_t kDiskBlockRatio = kBlockSize / kDeviceBlockSize;

TEST(FlushRequestsTest, FlushOneRequest) {
    class TestTransactionManager : public MockTransactionManager {
        zx_status_t Transaction(block_fifo_request_t* requests, size_t count) final {
            EXPECT_EQ(1, count);
            EXPECT_EQ(1 * kDiskBlockRatio, requests[0].vmo_offset);
            EXPECT_EQ(2 * kDiskBlockRatio, requests[0].dev_offset);
            EXPECT_EQ(3 * kDiskBlockRatio, requests[0].length);
            return ZX_OK;
        }
    } manager;
    fbl::Vector<Operation> operations;
    operations.push_back(Operation { OperationType::kWrite, 1, 2, 3 });
    EXPECT_EQ(ZX_OK, FlushWriteRequests(&manager, operations, 2));
}

TEST(FlushRequestsTest, FlushManyRequests) {
    class TestTransactionManager : public MockTransactionManager {
        zx_status_t Transaction(block_fifo_request_t* requests, size_t count) final {
            EXPECT_EQ(2, count);
            EXPECT_EQ(1 * kDiskBlockRatio, requests[0].vmo_offset);
            EXPECT_EQ(2 * kDiskBlockRatio, requests[0].dev_offset);
            EXPECT_EQ(3 * kDiskBlockRatio, requests[0].length);
            EXPECT_EQ(4 * kDiskBlockRatio, requests[1].vmo_offset);
            EXPECT_EQ(5 * kDiskBlockRatio, requests[1].dev_offset);
            EXPECT_EQ(6 * kDiskBlockRatio, requests[1].length);
            return ZX_OK;
        }
    } manager;
    fbl::Vector<Operation> operations;
    operations.push_back(Operation { OperationType::kWrite, 1, 2, 3 });
    operations.push_back(Operation { OperationType::kWrite, 4, 5, 6 });
    EXPECT_EQ(ZX_OK, FlushWriteRequests(&manager, operations, 2));
}

TEST(FlushRequestsTest, BadFlush) {
    class TestTransactionManager : public MockTransactionManager {
        zx_status_t Transaction(block_fifo_request_t* requests, size_t count) final {
            return ZX_ERR_NOT_SUPPORTED;
        }
    } manager;
    fbl::Vector<Operation> operations;
    operations.push_back(Operation { OperationType::kWrite, 1, 2, 3 });
    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, FlushWriteRequests(&manager, operations, 2));
}

} // namespace
} // namespace blobfs
