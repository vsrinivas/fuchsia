// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file describes the structure used to allocate
// from an on-disk bitmap.

#pragma once

#include <fbl/function.h>
#include <fbl/unique_ptr.h>
#include <fs/block-txn.h>
#include <fs/mapped-vmo.h>

#include <minfs/format.h>
#include <minfs/block-txn.h>

namespace minfs {

#ifdef __Fuchsia__
using RawBitmap = bitmap::RawBitmapGeneric<bitmap::VmoStorage>;
#else
using RawBitmap = bitmap::RawBitmapGeneric<bitmap::DefaultStorage>;
#endif

class Allocator {
public:
    // Callback used to grow the bitmap.
    // If successful, returns the size of the new bitmap in |pool_size|.
    using GrowHandler = fbl::Function<zx_status_t(WriteTxn* txn, size_t* pool_size)>;

    // Callback used to update callers about global allocation
    // stats.
    using UsageHandler = fbl::Function<void(WriteTxn* txn, size_t used)>;

    zx_status_t Initialize(const Bcache* bc, ReadTxn* txn, GrowHandler grow_cb,
                           UsageHandler usage_cb, blk_t start_block,
                           size_t pool_used, size_t pool_size);

    // Allocate a new item.
    zx_status_t Allocate(WriteTxn* txn, size_t hint, size_t* out_index);

    // Free an item from the allocator.
    void Free(WriteTxn* txn, size_t index);

private:
    friend class MinfsChecker;

    zx_status_t Extend(WriteTxn* txn);

    // Write back the allocation of the following items to disk.
    void Persist(WriteTxn* txn, size_t index, size_t count);

    RawBitmap map_;

    GrowHandler grow_cb_;
    UsageHandler usage_cb_;

    blk_t start_block_;
    size_t pool_used_;
    // TODO(smklein): Keep a counter of the "reserved but not allocated" blocks
    // here when implementing delayed allocation.
};

} // namespace minfs
