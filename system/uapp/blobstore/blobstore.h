// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <bitmap/raw-bitmap.h>
#include <bitmap/storage.h>
#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <mxtl/algorithm.h>
#include <mxtl/intrusive_double_list.h>
#include <mxtl/intrusive_wavl_tree.h>
#include <mxtl/macros.h>
#include <mxtl/ref_counted.h>
#include <mxtl/ref_ptr.h>
#include <mxtl/unique_free_ptr.h>

#include <magenta/types.h>

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

using RawBitmap = bitmap::RawBitmapGeneric<bitmap::VmoStorage>;

// clang-format off

constexpr uint64_t kBlobstoreMagic0  = (0xac2153479e694d21ULL);
constexpr uint64_t kBlobstoreMagic1  = (0x985000d4d4d3d314ULL);
constexpr uint32_t kBlobstoreVersion = 0x00000002;

constexpr uint32_t kBlobstoreFlagClean      = 1;
constexpr uint32_t kBlobstoreFlagDirty      = 2;
constexpr uint32_t kBlobstoreBlockSize      = 8192;
constexpr uint32_t kBlobstoreBlockBits      = (kBlobstoreBlockSize * 8);
constexpr uint32_t kBlobstoreBlockMapStart  = 1;
constexpr uint32_t kBlobstoreInodeSize      = 64;
constexpr uint32_t kBlobstoreInodesPerBlock = (kBlobstoreBlockSize / kBlobstoreInodeSize);

static_assert(kBlobstoreBlockSize % PAGE_SIZE == 0,
              "Blobstore block size should be a multiple of page size");

// Notes:
// - block 0 is always allocated
// - inode 0 is never used, should be marked allocated but ignored

typedef struct {
    uint64_t magic0;
    uint64_t magic1;
    uint32_t version;
    uint32_t flags;
    uint32_t block_size;       // 8K typical
    uint64_t block_count;      // Number of blocks in this area
    uint64_t inode_count;      // Number of blobs in this area
    uint64_t alloc_block_count; // Total number of allocated blocks
    uint64_t alloc_inode_count; // Total number of allocated blobs
    uint64_t blob_header_next; // Block containing next blobstore, or zero if this is the last one
} blobstore_info_t;

constexpr uint64_t BlockMapStartBlock() {
    return kBlobstoreBlockMapStart;
}

constexpr uint64_t BlockMapBlocks(const blobstore_info_t& info) {
    return mxtl::roundup(info.block_count, kBlobstoreBlockBits) / kBlobstoreBlockBits;
}

constexpr uint64_t NodeMapStartBlock(const blobstore_info_t& info) {
    // Node map immediately follows the block map
    return BlockMapStartBlock() + BlockMapBlocks(info);
}

constexpr uint64_t NodeMapBlocks(const blobstore_info_t& info) {
    return mxtl::roundup(info.inode_count, kBlobstoreInodesPerBlock) / kBlobstoreInodesPerBlock;
}

constexpr uint64_t DataStartBlock(const blobstore_info_t& info) {
    // Data immediately follows the node map
    return NodeMapStartBlock(info) + NodeMapBlocks(info);
}

// States of 'Blob' identified via start block.
constexpr uint64_t kStartBlockFree     = 0;
constexpr uint64_t kStartBlockReserved = 1;
constexpr uint64_t kStartBlockMinimum  = 2; // Smallest 'data' block possible

using digest::Digest;
typedef struct {
    uint8_t  merkle_root_hash[Digest::kLength];
    uint64_t start_block;
    uint64_t num_blocks;
    uint64_t blob_size;
    uint64_t reserved;
} blobstore_inode_t;

static_assert(sizeof(blobstore_inode_t) == kBlobstoreInodeSize,
              "Blobstore Inode size is wrong");
static_assert(kBlobstoreBlockSize % kBlobstoreInodeSize == 0,
              "Blobstore Inodes should fit cleanly within a blobstore block");

// Number of blocks reserved for the blob itself
constexpr uint64_t BlobDataBlocks(const blobstore_inode_t& blobNode) {
    return mxtl::roundup(blobNode.blob_size, kBlobstoreBlockSize) / kBlobstoreBlockSize;
}

void* GetBlock(const RawBitmap& bitmap, uint32_t blkno);
void* GetBitBlock(const RawBitmap& bitmap, uint32_t* blkno_out, uint32_t bitno);
