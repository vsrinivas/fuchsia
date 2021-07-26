// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/storage/blobfs/common.h"

#include <zircon/errors.h>

#include <limits>

#include <gtest/gtest.h>

#include "src/storage/blobfs/blob_layout.h"
#include "src/storage/blobfs/format.h"

namespace blobfs {
namespace {

constexpr uint64_t kBlockCount = 1 << 10;

TEST(CommonTest, PaddedBlobLayoutFormatIsRoundTrippedThroughTheSuperblock) {
  BlobLayoutFormat format = BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart;
  Superblock info;
  InitializeSuperblock(kBlockCount, {.blob_layout_format = format}, &info);
  EXPECT_EQ(GetBlobLayoutFormat(info), format);
}

TEST(CommonTest, CompactBlobLayoutFormatIsRoundTrippedThroughTheSuperblock) {
  BlobLayoutFormat format = BlobLayoutFormat::kCompactMerkleTreeAtEnd;
  Superblock info;
  InitializeSuperblock(kBlockCount, {.blob_layout_format = format}, &info);
  EXPECT_EQ(GetBlobLayoutFormat(info), format);
}

TEST(CommonTest, InodesRoundedUpToFillBlock) {
  Superblock info;
  InitializeSuperblock(kBlockCount,
                       {.num_inodes = kBlobfsDefaultInodeCount + kBlobfsInodesPerBlock - 1}, &info);
  EXPECT_EQ(info.inode_count, kBlobfsDefaultInodeCount + kBlobfsInodesPerBlock);
}

TEST(CommonTest, TooFewInodesFailsCheck) {
  Superblock info;
  static_assert(kBlobfsDefaultInodeCount > kBlobfsInodesPerBlock);
  InitializeSuperblock(kBlockCount, {.num_inodes = 0}, &info);
  EXPECT_EQ(ZX_ERR_NO_SPACE, CheckSuperblock(&info, std::numeric_limits<uint64_t>::max(), true));
}

}  // namespace
}  // namespace blobfs
