// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/device/block.h>
#include <mxtl/algorithm.h>
#include <mxtl/macros.h>

#include <fs/vfs.h>

#include "minfs.h"
#include "misc.h"

namespace minfs {

// Enqueue multiple writes (or reads) to the underlying block device
// by shoving them into a simple array, to avoid duplicated ops
// within a single operation.
//
// TODO(smklein): This obviously has plenty of room for
// improvement, including:
// - Sorting blocks, combining ranges
// - Writing from multiple buffers (instead of one)
// - Cross-operation writeback delays
template <typename IdType, bool Write>
class BlockTxn;

#ifdef __Fuchsia__

template <bool Write>
class BlockTxn <vmoid_t, Write> {
public:
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(BlockTxn);
    BlockTxn(Bcache* bc) : bc_(bc), count_(0) {}
    ~BlockTxn() {
        Flush();
    }

    // Identify that a block should be written to disk
    // as a later point in time.
    void Enqueue(vmoid_t id, uint32_t relative_block, uint32_t absolute_block, uint32_t nblocks) {
        for (size_t i = 0; i < count_; i++) {
            if (requests_[i].vmoid != id) {
                continue;
            }

            if (requests_[i].vmo_offset == relative_block) {
                // Take the longer of the operations (if operating on the same
                // blocks).
                requests_[i].length = (requests_[i].length > nblocks) ? requests_[i].length : nblocks;
                return;
            } else if ((requests_[i].vmo_offset + requests_[i].length == relative_block) &&
                       (requests_[i].dev_offset + requests_[i].length == absolute_block)) {
                // Combine with the previous request, if immediately following.
                requests_[i].length += nblocks;
                return;
            }
        }

        requests_[count_].txnid = bc_->TxnId();
        requests_[count_].vmoid = id;
        // NOTE: It's easier to compare everything when dealing
        // with blocks (not offsets!) so the following are described in
        // terms of blocks until we Flush().
        requests_[count_].vmo_offset = relative_block;
        requests_[count_].dev_offset = absolute_block;
        requests_[count_].length = nblocks;
        count_++;

        if (count_ == MAX_TXN_MESSAGES) {
            // TODO(smklein): Maybe panic (on write) instead, for metadata?
            // TODO(smklein): We could buffer more messages than this -- just
            // send then in MAX_TXN_MESSAGES increments.
            Flush();
        }
    }

    // Activate the transaction
    mx_status_t Flush();

private:
    Bcache* bc_;
    size_t count_;
    block_fifo_request_t requests_[MAX_TXN_MESSAGES];
};

template <bool Write>
inline mx_status_t BlockTxn<vmoid_t, Write>::Flush() {
    for (size_t i = 0; i < count_; i++) {
        requests_[i].opcode = Write ? BLOCKIO_WRITE : BLOCKIO_READ;
        requests_[i].vmo_offset *= kMinfsBlockSize;
        requests_[i].dev_offset *= kMinfsBlockSize;
        requests_[i].length *= kMinfsBlockSize;
    }
    mx_status_t status = NO_ERROR;
    if (count_ != 0) {
        status = bc_->Txn(requests_, count_);
    }
    count_ = 0;
    return status;
}

using WriteTxn = BlockTxn<vmoid_t, true>;
using ReadTxn = BlockTxn<vmoid_t, false>;

#else

// To simplify host-side requests, they are written
// through immediately, and cannot be buffered.
template <bool Write>
class BlockTxn<const void*, Write> {
public:
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(BlockTxn);
    BlockTxn(Bcache* bc) : bc_(bc) {}
    ~BlockTxn() { Flush(); }

    // Identify that a block should be written to disk
    // as a later point in time.
    void Enqueue(const void* id, uint32_t relative_block,
                 uint32_t absolute_block, uint32_t nblocks) {
        for (size_t b = 0; b < nblocks; b++) {
            if (Write) {
                bc_->Writeblk(absolute_block + b, GetBlock(id, relative_block + b));
            } else {
                bc_->Readblk(absolute_block + b, GetBlock(id, relative_block + b));
            }
        }
    }

    // Activate the transaction (do nothing)
    mx_status_t Flush() { return NO_ERROR; }

private:
    Bcache* bc_;
};

using WriteTxn = BlockTxn<const void*, true>;
using ReadTxn = BlockTxn<const void*, false>;

#endif

} // namespace minfs
