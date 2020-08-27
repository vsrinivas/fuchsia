// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_SUPERBLOCK_H_
#define SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_SUPERBLOCK_H_

#include <stdint.h>

#include "constants.h"

namespace block_verity {

// This is the packed, on-disk structure of a block-verity superblock.  Integral
// fields larger than one byte should be serialized in little-endian form.
struct __PACKED Superblock {
  uint8_t magic[16];  // expected to be kBlockVerityMagic, which is "block-verity-v1\0"
  uint64_t block_count;
  uint32_t block_size;              // size of each block, in bytes.
  uint32_t hash_function;           // expected to be kSHA256HashTag, which is 1
  uint8_t integrity_root_hash[32];  // SHA256 hash of root integrity block
  uint8_t pad[4032];
};
static_assert(sizeof(Superblock) == kBlockSize, "sizeof(Superblock) != kBlockSize");

}  // namespace block_verity

#endif  // SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_SUPERBLOCK_H_
