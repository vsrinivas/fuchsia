// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests vnode behavior.

// clang-format off
#include <memory>

#include <zircon/assert.h>
#include <zxtest/zxtest.h>

#include "minfs-private.h"
#include "vnode.h"
// clang-format on

namespace minfs {

TEST(VnodeBlockOffsetToIndirectVmoSize, DirectBlocks) {
  for (uint32_t block_offset = 0; block_offset < kMinfsDirect; block_offset++) {
    ASSERT_EQ(VnodeBlockOffsetToIndirectVmoSize(block_offset), 0);
  }
}

TEST(VnodeBlockOffsetToIndirectVmoSize, IndirectBlocks) {
  for (uint32_t indirect_offset = 0; indirect_offset < (kMinfsIndirect * kMinfsDirectPerIndirect);
       indirect_offset++) {
    uint32_t block_offset = kMinfsDirect + indirect_offset;
    // Indirect vmo is initialized to contain all indirect blocks and double indirect blocks.
    constexpr uint64_t kInitVmoSize = (kMinfsIndirect + kMinfsDoublyIndirect) * kMinfsBlockSize;
    ASSERT_EQ(VnodeBlockOffsetToIndirectVmoSize(block_offset), kInitVmoSize);
  }
}

TEST(VnodeBlockOffsetToIndirectVmoSize, DoubleIndirectBlocks) {
  constexpr uint32_t kIndirectAddressableVnodeBlockOffset =
      kMinfsDirect + (kMinfsIndirect * kMinfsDirectPerIndirect) - 1;

  uint64_t vmo_size_for_indirect_blocks =
      VnodeBlockOffsetToIndirectVmoSize(kIndirectAddressableVnodeBlockOffset);

  uint32_t block_offset = kIndirectAddressableVnodeBlockOffset + kMinfsDirectPerIndirect;
  ASSERT_GT(VnodeBlockOffsetToIndirectVmoSize(block_offset), vmo_size_for_indirect_blocks);

  // All the needed size for the indirect vmo to allow file grow to maximum extent possible is
  // allocated in one go. So vmo size for max_offset should not be different from vmo size of
  // block_offset defined above.
  uint32_t max_offset = kIndirectAddressableVnodeBlockOffset + kMinfsDirectPerDindirect;
  ASSERT_EQ(VnodeBlockOffsetToIndirectVmoSize(max_offset),
            VnodeBlockOffsetToIndirectVmoSize(block_offset));
}

}  // namespace minfs
