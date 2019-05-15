// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <blobfs/allocator.h>
#include <blobfs/writeback-queue.h>
#include <blobfs/writeback-work.h>
#include <fbl/auto_lock.h>
#include <fbl/vector.h>
#include <zxtest/zxtest.h>

#include <optional>

namespace blobfs {

constexpr uint32_t kBlockSize = 8192;
constexpr groupid_t kGroupID = 2;
constexpr uint32_t kDeviceBlockSize = 1024;
constexpr size_t kWritebackCapacity = 8;

// Callback for MockTransactionManager to invoke on calls to Transaction(). |request| is performed
// on the provided |vmo|.
using TransactionCallback = fbl::Function<zx_status_t(const block_fifo_request_t& request,
                                                      const zx::vmo& vmo)>;

// A simplified TransactionManager to be used when unit testing structures which require one (e.g.
// WritebackQueue, Journal). Allows vmos to be attached/detached and a customized callback to be
// invoked on transaction completion.
// This class is thread-safe.
class MockTransactionManager : public TransactionManager {
public:
    MockTransactionManager() {
        ASSERT_OK(WritebackQueue::Create(this, kWritebackCapacity, &writeback_));
    }

    ~MockTransactionManager() = default;

    // Sets the |callback| to be invoked for each request on calls to Transaction().
    void SetTransactionCallback(TransactionCallback callback) {
        fbl::AutoLock lock(&lock_);
        transaction_callback_ = std::move(callback);
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

    zx_status_t Transaction(block_fifo_request_t* requests, size_t count) override;

    const Superblock& Info() const final {
        return superblock_;
    }

    zx_status_t AddInodes(fzl::ResizeableVmoMapper* node_map) final {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t AddBlocks(size_t nblocks, RawBitmap* map) final {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t AttachVmo(const zx::vmo& vmo, vmoid_t* out) final;

    zx_status_t DetachVmo(vmoid_t vmoid) final;

    BlobfsMetrics& LocalMetrics() final {
        return metrics_;
    }

    size_t WritebackCapacity() const final {
        return kWritebackCapacity;
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
    fbl::Vector<std::optional<zx::vmo>> attached_vmos_ __TA_GUARDED(lock_);
    TransactionCallback transaction_callback_ __TA_GUARDED(lock_);
    fbl::Mutex lock_;
};

// A trivial space manager, incapable of resizing.
class MockSpaceManager : public SpaceManager {
public:
    Superblock& MutableInfo() {
        return superblock_;
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
        return ZX_ERR_NOT_SUPPORTED;
    }
    zx_status_t DetachVmo(vmoid_t vmoid) final {
        return ZX_ERR_NOT_SUPPORTED;
    }

private:
    Superblock superblock_{};
};

// Create a block and node map of the requested size, update the superblock of
// the |space_manager|, and create an allocator from this provided info.
void InitializeAllocator(size_t blocks, size_t nodes, MockSpaceManager* space_manager,
                         fbl::unique_ptr<Allocator>* out);

// Force the allocator to become maximally fragmented by allocating
// every-other block within up to |blocks|.
void ForceFragmentation(Allocator* allocator, size_t blocks);

// Save the extents within |in| in a non-reserved vector |out|.
void CopyExtents(const fbl::Vector<ReservedExtent>& in, fbl::Vector<Extent>* out);

// Save the nodes within |in| in a non-reserved vector |out|.
void CopyNodes(const fbl::Vector<ReservedNode>& in, fbl::Vector<uint32_t>* out);

} // namespace blobfs
