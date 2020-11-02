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

TEST(CommonTest, GetBlobLayoutFormatWithValidFormatIsCorrect) {
  BlobLayoutFormat format = BlobLayoutFormat::kCompactMerkleTreeAtEnd;
  Superblock info = {
      .blob_layout_format = static_cast<uint8_t>(format),
  };
  EXPECT_EQ(GetBlobLayoutFormat(info), format);
}

TEST(CommonTest, GetBlobLayoutFormatWithInvalidFormatPanics) {
  Superblock info = {
      .blob_layout_format = 255,
  };
  EXPECT_DEATH(GetBlobLayoutFormat(info), "Invalid blob layout format");
}

TEST(CommonTest, CheckSuperblockWithValidBlobLayoutFormatIsOk) {
  Superblock info;
  InitializeSuperblock(kBlockCount,
                       {.blob_layout_format = BlobLayoutFormat::kCompactMerkleTreeAtEnd}, &info);
  EXPECT_EQ(CheckSuperblock(&info, kBlockCount), ZX_OK);
}

TEST(CommonTest, CheckSuperblockWithInvalidBlobLayoutFormatIsError) {
  Superblock info;
  InitializeSuperblock(kBlockCount, {.blob_layout_format = static_cast<BlobLayoutFormat>(255)},
                       &info);
  EXPECT_EQ(CheckSuperblock(&info, kBlockCount), ZX_ERR_INVALID_ARGS);
}

}  // namespace
}  // namespace blobfs
