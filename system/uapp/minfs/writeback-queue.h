// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mxtl/algorithm.h>
#include <mxtl/intrusive_hash_table.h>
#include <mxtl/intrusive_single_list.h>
#include <mxtl/macros.h>
#include <mxtl/ref_ptr.h>
#include <mxtl/unique_ptr.h>

#include <fs/vfs.h>

#include "minfs.h"
#include "misc.h"

namespace minfs {

typedef struct {
    uint32_t relative_block;
    uint32_t absolute_block;
} block_txn_t;

// Enqueue multiple writes to the underlying block device
// by shoving them into a simple array, to avoid duplicated
// writes within a single operation.
//
// TODO(smklein): This obviously has plenty of room for
// improvement, including:
// - Sorting dirty blocks, combining ranges
// - Writing from multiple buffers (instead of one)
// - Cross-operation writeback delays
template <size_t BufferCap=128>
class WritebackQueue {
public:
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(WritebackQueue);
    WritebackQueue(Bcache* bc, const void* data) : bc_(bc), data_(data), count_(0) {}
    ~WritebackQueue() {
        Flush();
    }

    // Identify that a block should be written to disk
    // as a later point in time.
    void EnqueueDirty(uint32_t relative_block, uint32_t absolute_block) {
        for (size_t i = 0; i < count_; i++) {
            if (blocks_[i].relative_block == relative_block) {
                return;
            }
        }

        blocks_[count_].relative_block = relative_block;
        blocks_[count_].absolute_block = absolute_block;
        count_++;

        if (count_ == BufferCap) {
            Flush();
        }
    }

    // Write all enqueued blocks to disk.
    void Flush() {
        for (size_t i = 0; i < count_; i++) {
            bc_->Writeblk(blocks_[i].absolute_block,
                          GetNthBlock(data_, blocks_[i].relative_block));
        }
        count_ = 0;
    }

private:
    Bcache* bc_;
    const void* data_;
    block_txn_t blocks_[BufferCap];
    size_t count_;
};

} // namespace minfs
