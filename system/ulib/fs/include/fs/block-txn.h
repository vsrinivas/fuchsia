// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/device/block.h>
#include <fbl/algorithm.h>
#include <fbl/macros.h>
#include <fbl/vector.h>

#include <fs/vfs.h>

namespace fs {

// Access the "blkno"-th block within data.
// "blkno = 0" corresponds to the first block within data.
template <size_t BlockSize>
void* GetBlock(const void* data, uint64_t blkno) {
    assert(BlockSize <= (blkno + 1) * BlockSize); // Avoid overflow
    return (void*)((uintptr_t)(data) + (uintptr_t)(BlockSize * blkno));
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
template <typename IdType, bool Write, size_t BlockSize, typename TxnHandler>
class BlockTxn;

#ifdef __Fuchsia__

template <bool Write, size_t BlockSize, typename TxnHandler>
class BlockTxn <vmoid_t, Write, BlockSize, TxnHandler> {
public:
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(BlockTxn);
    explicit BlockTxn(TxnHandler* handler) : handler_(handler) {}
    ~BlockTxn() {
        Flush();
    }

    // Identify that a block should be written to disk
    // as a later point in time.
    void Enqueue(vmoid_t id, uint64_t vmo_offset, uint64_t dev_offset, uint64_t nblocks) {
        for (size_t i = 0; i < requests_.size(); i++) {
            if (requests_[i].vmoid != id) {
                continue;
            }

            if (requests_[i].vmo_offset == vmo_offset) {
                // Take the longer of the operations (if operating on the same
                // blocks).
                requests_[i].length = (requests_[i].length > nblocks) ? requests_[i].length : nblocks;
                return;
            } else if ((requests_[i].vmo_offset + requests_[i].length == vmo_offset) &&
                       (requests_[i].dev_offset + requests_[i].length == dev_offset)) {
                // Combine with the previous request, if immediately following.
                requests_[i].length += nblocks;
                return;
            }
        }

        block_fifo_request_t request;
        request.txnid = handler_->TxnId();
        request.vmoid = id;
        // NOTE: It's easier to compare everything when dealing
        // with blocks (not offsets!) so the following are described in
        // terms of blocks until we Flush().
        request.length = nblocks;
        request.vmo_offset = vmo_offset;
        request.dev_offset = dev_offset;
        requests_.push_back(fbl::move(request));
    }

    // Activate the transaction
    zx_status_t Flush();

private:
    TxnHandler* handler_;
    fbl::Vector<block_fifo_request_t> requests_;
};

template <bool Write, size_t BlockSize, typename TxnHandler>
inline zx_status_t BlockTxn<vmoid_t, Write, BlockSize, TxnHandler>::Flush() {
    // Convert 'filesystem block' units to 'disk block' units.
    const size_t kBlockFactor = BlockSize / handler_->BlockSize();
    for (size_t i = 0; i < requests_.size(); i++) {
        requests_[i].opcode = Write ? BLOCKIO_WRITE : BLOCKIO_READ;
        requests_[i].vmo_offset *= kBlockFactor;
        requests_[i].dev_offset *= kBlockFactor;
        requests_[i].length *= kBlockFactor;
    }
    zx_status_t status = ZX_OK;
    if (requests_.size() != 0) {
        status = handler_->Txn(requests_.get(), requests_.size());
    }
    requests_.reset();
    return status;
}

template <size_t BlockSize, typename TxnHandler>
using WriteTxn = BlockTxn<vmoid_t, true, BlockSize, TxnHandler>;
template <size_t BlockSize, typename TxnHandler>
using ReadTxn = BlockTxn<vmoid_t, false, BlockSize, TxnHandler>;

#else

// To simplify host-side requests, they are written
// through immediately, and cannot be buffered.
template <bool Write, size_t BlockSize, typename TxnHandler>
class BlockTxn<const void*, Write, BlockSize, TxnHandler> {
public:
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(BlockTxn);
    BlockTxn(TxnHandler* handler) : handler_(handler) {}
    ~BlockTxn() { Flush(); }

    // Identify that a block should be written to disk
    // as a later point in time.
    void Enqueue(const void* id, uint64_t vmo_offset, uint64_t dev_offset, uint64_t nblocks) {
        for (size_t b = 0; b < nblocks; b++) {
            if (Write) {
                handler_->Writeblk(dev_offset + b, GetBlock<BlockSize>(id, vmo_offset + b));
            } else {
                handler_->Readblk(dev_offset + b, GetBlock<BlockSize>(id, vmo_offset + b));
            }
        }
    }

    // Activate the transaction (do nothing)
    zx_status_t Flush() { return ZX_OK; }

private:
    TxnHandler* handler_;
};

template <size_t BlockSize, typename TxnHandler>
using WriteTxn = BlockTxn<const void*, true, BlockSize, TxnHandler>;
template <size_t BlockSize, typename TxnHandler>
using ReadTxn = BlockTxn<const void*, false, BlockSize, TxnHandler>;

#endif

} // namespace fs
