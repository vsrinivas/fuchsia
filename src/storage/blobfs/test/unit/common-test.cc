// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <zircon/errors.h>

#include <limits>

#include <blobfs/blob-layout.h>
#include <blobfs/common.h>
#include <blobfs/format.h>
#include <gtest/gtest.h>

namespace blobfs {
namespace {

constexpr uint64_t kBlockCount = 1 << 10;

TEST(CommonTest, PaddedBlobLayoutFormatIsRoundTrippedThroughTheSuperblock) {
  BlobLayoutFormat format = BlobLayoutFormat::kPaddedMerkleTreeAtStart;
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

}  // namespace
}  // namespace blobfs
