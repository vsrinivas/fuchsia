// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "src/devices/block/drivers/block-verity/geometry.h"

namespace {

TEST(GeometryTest, IntegrityShapeFor4kSHA256) {
  block_verity::IntegrityShape i = block_verity::IntegrityShapeFor(4096, 32, 8192);
  // The optimal shape is 1 superblock, 65 integrity blocks, and 8126 data blocks.
  // 8126 / 128 = 63.48 or so.
  // We need 64 direct hash blocks, and one indirect hash block, which contains
  // hashes of the direct hash blocks (which themselves contain hashes of the
  // data blocks).
  ASSERT_EQ(i.integrity_block_count, 65);
  ASSERT_EQ(i.tree_depth, 2);
}

TEST(GeometryTest, IntegrityShapeForAssertsIfHashNotMultipleOfBlockSize) {
  ASSERT_DEATH([] { block_verity::IntegrityShapeFor(4096, 33, 8192); },
               "IntegrityShapeFor should assert if block_size modulo hash_size is not 0");
}

TEST(GeometryTest, BestSplitFor) {
  block_verity::BlockAllocation a = block_verity::BestSplitFor(4096, 32, 3);
  ASSERT_EQ(a.superblock_count, 1);
  ASSERT_EQ(a.padded_integrity_block_count, 1);
  ASSERT_EQ(a.data_block_count, 1);
  ASSERT_EQ(a.superblock_count + a.padded_integrity_block_count + a.data_block_count, 3);

  // Verify that we smoothly allocate additional blocks, and that we always
  // allocate all blocks, from the smallest possible partition (3 blocks) up to
  // ~32MiB on 4k blocks with SHA256 hash function.
  block_verity::BlockAllocation prev = a;
  for (uint64_t block_count = 4; block_count <= 8192; block_count++) {
    block_verity::BlockAllocation ba = block_verity::BestSplitFor(4096, 32, block_count);
    ASSERT_EQ(ba.superblock_count + ba.padded_integrity_block_count + ba.data_block_count,
              block_count);
    ASSERT_EQ(ba.superblock_count, 1);

    bool changed_integrity = (ba.padded_integrity_block_count != prev.padded_integrity_block_count);
    bool changed_data = (ba.data_block_count != prev.data_block_count);
    ASSERT_TRUE(changed_integrity != changed_data);
    if (changed_integrity) {
      ASSERT_EQ(ba.padded_integrity_block_count, prev.padded_integrity_block_count + 1);
    }
    if (changed_data) {
      ASSERT_EQ(ba.data_block_count, prev.data_block_count + 1);
    }
    prev = ba;
  }
}

TEST(GeometryTest, BestSplitForAssertsIfTooSmall) {
  ASSERT_DEATH([] { block_verity::BestSplitFor(4096, 32, 2); },
               "BestSplitFor should assert if total_blocks is less than 3");
}

}  // namespace
