// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/assert.h>
#include <zircon/device/block.h>
#include <fbl/algorithm.h>
#include <fbl/macros.h>
#include <fbl/vector.h>

#include <fs/vfs.h>

namespace fs {

// Access the "blkno"-th block within data.
// "blkno = 0" corresponds to the first block within data.
inline void* GetBlock(uint64_t block_size, const void* data, uint64_t blkno) {
    ZX_ASSERT(block_size <= (blkno + 1) * block_size); // Avoid overflow
    return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(data) +
                                   static_cast<uintptr_t>(block_size * blkno));
}

// Enqueue multiple writes (or reads) to the underlying block device
// by shoving them into a simple array, to avoid duplicated ops
// within a single operation.
//
// TODO(smklein): This obviously has plenty of room for
// improvement, including:
// - Sorting blocks, combining ranges
// - Writing from multiple buffers (instead of one)
// - Cross-operation writeback delays
class BlockTxn;

// TransactionHandler defines the interface the must be fulfilled
// for an entity to issue transactions to the underlying device.
class TransactionHandler {
public:
    virtual ~TransactionHandler() {};

    // Acquire the block size of the mounted filesystem.
    // It is assumed that all inputs to the TransactionHandler
    // interface are in |FsBlockSize()|-sized blocks.
    virtual uint32_t FsBlockSize() const = 0;

#ifdef __Fuchsia__
    // Acquires the block group on which the transaction should be issued.
    virtual groupid_t BlockGroupID() = 0;

    // Acquires the block size of the underlying device.
    virtual uint32_t DeviceBlockSize() const = 0;

    // Issues a group of requests to the underlying device and waits
    // for them to complete.
    virtual zx_status_t Transaction(block_fifo_request_t* requests, size_t count) = 0;
#else
    // Reads block |bno| from the device into the buffer provided by |data|.
    virtual zx_status_t Readblk(uint32_t bno, void* data) = 0;

    // Writes block |bno| from the buffer provided by |data| to the device.
    virtual zx_status_t Writeblk(uint32_t bno, const void* data) = 0;
#endif
};

#ifdef __Fuchsia__

class BlockTxn {
public:
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(BlockTxn);
    explicit BlockTxn(TransactionHandler* handler) : handler_(handler) {}
    ~BlockTxn() {
        Transact();
    }

    // Identify that an operation should be committed to disk
    // at a later point in time.
    void EnqueueOperation(uint32_t op, vmoid_t id, uint64_t vmo_offset,
                          uint64_t dev_offset, uint64_t nblocks) {
        // TODO(ZX-2253): Remove this assertion.
        ZX_ASSERT_MSG(nblocks < UINT32_MAX, "Too many blocks");
        uint32_t blocks = static_cast<uint32_t>(nblocks);
        for (size_t i = 0; i < requests_.size(); i++) {
            if (requests_[i].vmoid != id || requests_[i].opcode != op) {
                continue;
            }

            if (requests_[i].vmo_offset == vmo_offset) {
                // Take the longer of the operations (if operating on the same
                // blocks).
                if (requests_[i].length <= blocks) {
                    requests_[i].length = blocks;
                }
                return;
            } else if ((requests_[i].vmo_offset + requests_[i].length == vmo_offset) &&
                       (requests_[i].dev_offset + requests_[i].length == dev_offset)) {
                // Combine with the previous request, if immediately following.
                requests_[i].length += blocks;
                return;
            }
        }

        block_fifo_request_t request;
        request.opcode = op;
        request.group = handler_->BlockGroupID();
        request.vmoid = id;
        // NOTE: It's easier to compare everything when dealing
        // with blocks (not offsets!) so the following are described in
        // terms of blocks until we Transact().
        request.length = blocks;
        request.vmo_offset = vmo_offset;
        request.dev_offset = dev_offset;
        requests_.push_back(fbl::move(request));
    }

    // Activate the transaction.
    zx_status_t Transact() {
        // Convert 'filesystem block' units to 'disk block' units.
        const size_t kBlockFactor = handler_->FsBlockSize() / handler_->DeviceBlockSize();
        for (size_t i = 0; i < requests_.size(); i++) {
            requests_[i].vmo_offset *= kBlockFactor;
            requests_[i].dev_offset *= kBlockFactor;
            // TODO(ZX-2253): Remove this assertion.
            uint64_t length = requests_[i].length * kBlockFactor;
            ZX_ASSERT_MSG(length < UINT32_MAX, "Too many blocks");
            requests_[i].length = static_cast<uint32_t>(length);
        }
        zx_status_t status = ZX_OK;
        if (requests_.size() != 0) {
            status = handler_->Transaction(requests_.get(), requests_.size());
        }
        requests_.reset();
        return status;
    }

private:
    TransactionHandler* handler_;
    fbl::Vector<block_fifo_request_t> requests_;
};

#else

// To simplify host-side requests, they are written
// through immediately, and cannot be buffered.
class BlockTxn {
public:
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(BlockTxn);
    explicit BlockTxn(TransactionHandler* handler) : handler_(handler) {}
    ~BlockTxn() {
        Transact();
    }

    // Identify that an operation should be committed to disk
    // at a later point in time.
    void EnqueueOperation(uint32_t op, const void* id, uint64_t vmo_offset,
                          uint64_t dev_offset, uint64_t nblocks) {
        for (size_t b = 0; b < nblocks; b++) {
            void* data = GetBlock(handler_->FsBlockSize(), id, vmo_offset + b);
            if (op == BLOCKIO_WRITE) {
                handler_->Writeblk(dev_offset + b, data);
            } else if (op == BLOCKIO_READ) {
                handler_->Readblk(dev_offset + b, data);
            } else if (op == BLOCKIO_FLUSH) {
                // No-op.
            } else {
                assert(false); // Invalid operation.
            }
        }
    }

    // Activate the transaction (do nothing)
    zx_status_t Transact() {
        return ZX_OK;
    }

private:
    TransactionHandler* handler_;
};

#endif

// Provides a type-safe, low-cost abstraction over the |BlockTxn| class,
// allowing clients to avoid intermingling distinct operation types
// unless explicitly requested.
template <typename IdType, uint32_t Operation>
class TypedTxn {
public:
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(TypedTxn);
    explicit TypedTxn(TransactionHandler* handler) : txn_(handler) {}

    inline void Enqueue(IdType id, uint64_t vmo_offset, uint64_t dev_offset, uint64_t nblocks) {
        txn_.EnqueueOperation(Operation, id, vmo_offset, dev_offset, nblocks);
    }

    inline void EnqueueFlush() {
        IdType id = 0;
        uint64_t vmo_offset = 0;
        uint64_t dev_offset = 0;
        uint64_t nblocks = 0;
        txn_.EnqueueOperation(BLOCKIO_FLUSH, id, vmo_offset, dev_offset, nblocks);
    }

    inline zx_status_t Transact() {
        return txn_.Transact();
    }

private:
    BlockTxn txn_;
};

#ifdef __Fuchsia__

using WriteTxn = TypedTxn<vmoid_t, BLOCKIO_WRITE>;
using ReadTxn = TypedTxn<vmoid_t, BLOCKIO_READ>;

#else

using WriteTxn = TypedTxn<const void*, BLOCKIO_WRITE>;
using ReadTxn = TypedTxn<const void*, BLOCKIO_READ>;

#endif

} // namespace fs
