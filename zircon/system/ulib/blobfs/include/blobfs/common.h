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

// The minimum number of blocks that must be saved by
// compression to consider on-disk compression before writeback.
constexpr uint64_t kCompressionMinBlocksSaved = 8;
constexpr uint64_t kCompressionMinBytesSaved = kCompressionMinBlocksSaved * kBlobfsBlockSize;

#ifdef __Fuchsia__
using RawBitmap = bitmap::RawBitmapGeneric<bitmap::VmoStorage>;
#else
using RawBitmap = bitmap::RawBitmapGeneric<bitmap::DefaultStorage>;
#endif

void* GetBlock(const RawBitmap& bitmap, uint32_t blkno);
void* GetBitBlock(const RawBitmap& bitmap, uint32_t* blkno_out, uint32_t bitno);

zx_status_t readblk(int fd, uint64_t bno, void* data);
zx_status_t writeblk(int fd, uint64_t bno, const void* data);
zx_status_t CheckSuperblock(const Superblock* info, uint64_t max);
zx_status_t GetBlockCount(int fd, uint64_t* out);
int Mkfs(int fd, uint64_t block_count);

uint32_t MerkleTreeBlocks(const Inode& blobNode);

// Get a pointer to the nth block of the bitmap.
inline void* GetRawBitmapData(const RawBitmap& bm, uint64_t n) {
    assert(n * kBlobfsBlockSize < bm.size());             // Accessing beyond end of bitmap
    assert(kBlobfsBlockSize <= (n + 1) * kBlobfsBlockSize); // Avoid overflow
    return fs::GetBlock(kBlobfsBlockSize, bm.StorageUnsafe()->GetData(), n);
}

} // namespace blobfs
