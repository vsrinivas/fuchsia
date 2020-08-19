// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file describes the on-disk structure of Factoryfs.

#ifndef SRC_STORAGE_FACTORY_FACTORYFS_FORMAT_H_
#define SRC_STORAGE_FACTORY_FACTORYFS_FORMAT_H_

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <algorithm>
#include <limits>

#include <fbl/algorithm.h>
#include <fbl/macros.h>

namespace factoryfs {

// Location of superblock (block number).
constexpr uint64_t kSuperblockStart = 0;

// Total number of blocks needed to store the superblock.
constexpr uint32_t kFactoryfsSuperblockBlocks = 1;

// Location where directory entries start (block number).
constexpr uint32_t kDirenStartBlock = 1;

constexpr uint32_t kFactoryfsBlockSize = 4096;

struct __PACKED Superblock {
  uint64_t magic;  // Must be kFactoryFSMagic.
  uint32_t major_version;
  uint32_t minor_version;
  uint32_t flags;                      // Reserve for future use.
  uint32_t data_blocks;                // Total number of data blocks (in filesystem blocks)
  uint32_t directory_size;             // Size in bytes of all the directory entries
  uint32_t directory_entries;          // Number of directory entries.
  uint64_t create_time;                // Time of creation of all files.
  uint32_t block_size;                 // Filesystem block size.
  uint32_t directory_ent_blocks;       // Number of blocks for directory entries
  uint32_t directory_ent_start_block;  // start block for directory entries.
  uint32_t reserved[1011];             // Reserved for future use. Required to be zero
                                       // currently. Padded to align to block size.
};

static_assert(sizeof(Superblock) == kFactoryfsBlockSize, "factory info size is wrong");

// Each directory entry holds a pathname and gives the offset and size
// of the contents of the file by that name.
struct __PACKED DirectoryEntry {
  uint32_t name_len;  // Length of the name[] field at the end.
  uint32_t data_len;  // Length of the file in bytes.
  uint32_t data_off;  // Offset i.e block number where the file data starts.
  char name[];        // Pathname of the file, a UTF-8 string. It must not begin with a
                      // '/', but it may contain '/' separators for subdirectories.
                      // name does not a have trailing \0
};

// Returns the length of the DirectoryEntry structure required to hold a name of the given length.
// Each directory entry has a variable size of [16,268] that
// must be a multiple of 4 bytes.
constexpr uint32_t DirentSize(uint32_t namelen) {
  return sizeof(DirectoryEntry) + ((namelen + 3) & (~3));
}

constexpr uint64_t kFactoryfsMagic = 0xa55d3ff91e694d21;
constexpr uint32_t kFactoryfsMajorVersion = 0x00000001;
constexpr uint32_t kFactoryfsMinorVersion = 0x00000000;

// name_len must be > 1 and <= kFactoryfsMaxNameSize.
constexpr uint8_t kFactoryfsMaxNameSize = 255;

// The largest acceptable value of DirentSize(direntry->namelen).
constexpr uint32_t kFactoryfsMaxDirentSize = DirentSize(kFactoryfsMaxNameSize);

static_assert(kFactoryfsMaxNameSize >= NAME_MAX,
              "Factoryfs names must be large enough to hold NAME_MAX characters");

constexpr uint64_t DataBlocks(const Superblock& info) { return info.data_blocks; }

constexpr uint64_t TotalNonDataBlocks(const Superblock& info) {
  // 1 block for superblock added with the directory entry blocks.
  return 1 + info.directory_ent_blocks;
}

constexpr uint64_t TotalBlocks(const Superblock& info) {
  return TotalNonDataBlocks(info) + DataBlocks(info);
}

}  // namespace factoryfs

#endif  // SRC_STORAGE_FACTORY_FACTORYFS_FORMAT_H_
