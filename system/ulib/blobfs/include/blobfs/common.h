// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains functions that are shared between host
// and target implementations of Blobfs.

#pragma once

#include <bitmap/raw-bitmap.h>
#include <bitmap/storage.h>
#include <fbl/algorithm.h>
#include <fbl/macros.h>
#include <fs/block-txn.h>
#include <zircon/types.h>

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

#include <blobfs/format.h>

namespace blobfs {

#ifdef __Fuchsia__
using RawBitmap = bitmap::RawBitmapGeneric<bitmap::VmoStorage>;
#else
using RawBitmap = bitmap::RawBitmapGeneric<bitmap::DefaultStorage>;
#endif

void* GetBlock(const RawBitmap& bitmap, uint32_t blkno);
void* GetBitBlock(const RawBitmap& bitmap, uint32_t* blkno_out, uint32_t bitno);

zx_status_t readblk(int fd, uint64_t bno, void* data);
zx_status_t writeblk(int fd, uint64_t bno, const void* data);
zx_status_t blobfs_check_info(const blobfs_info_t* info, uint64_t max);
zx_status_t blobfs_get_blockcount(int fd, uint64_t* out);
int blobfs_mkfs(int fd, uint64_t block_count);

uint64_t MerkleTreeBlocks(const blobfs_inode_t& blobNode);

// Get a pointer to the nth block of the bitmap.
inline void* get_raw_bitmap_data(const RawBitmap& bm, uint64_t n) {
    assert(n * kBlobfsBlockSize < bm.size());             // Accessing beyond end of bitmap
    assert(kBlobfsBlockSize <= (n + 1) * kBlobfsBlockSize); // Avoid overflow
    return fs::GetBlock<kBlobfsBlockSize>(bm.StorageUnsafe()->GetData(), n);
}

} // namespace blobfs
