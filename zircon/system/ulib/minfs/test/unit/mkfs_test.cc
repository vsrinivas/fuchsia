// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <block-client/cpp/fake-device.h>
#include <minfs/format.h>
#include <minfs/fsck.h>
#include <zxtest/zxtest.h>

#include "minfs_private.h"

namespace minfs {
namespace {

using block_client::FakeBlockDevice;
using block_client::FakeFVMBlockDevice;

constexpr uint64_t kBlockCount = 1 << 15;
constexpr uint32_t kBlockSize = 512;

TEST(FormatFilesystemTest, FilesystemFormatClearsJournal) {
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);

  // Format the device.
  std::unique_ptr<Bcache> bcache;
  ASSERT_OK(Bcache::Create(std::move(device), kBlockCount, &bcache));
  ASSERT_OK(Mkfs(bcache.get()));

  // Before re-formatting, fill the journal with sentinel pages.
  Superblock superblock = {};
  ASSERT_OK(LoadSuperblock(bcache.get(), &superblock));
  std::unique_ptr<uint8_t[]> sentinel(new uint8_t[kMinfsBlockSize]);
  memset(sentinel.get(), 'a', kMinfsBlockSize);
  storage::VmoBuffer buffer;
  ASSERT_OK(buffer.Initialize(bcache.get(), JournalBlocks(superblock), kMinfsBlockSize,
                              "journal-buffer"));
  for (size_t i = 0; i < JournalBlocks(superblock); i++) {
    memcpy(buffer.Data(i), sentinel.get(), kMinfsBlockSize);
  }
  storage::Operation operation = {};
  operation.type = storage::OperationType::kWrite;
  operation.vmo_offset = 0;
  operation.dev_offset = JournalStartBlock(superblock);
  operation.length = JournalBlocks(superblock);
  ASSERT_OK(bcache->RunOperation(operation, &buffer));

  // Format the device. We expect this to clear the sentinel pages.
  ASSERT_OK(Mkfs(bcache.get()));

  // Verify that the device has written zeros to the expected location, overwriting
  // the sentinel pages.
  operation.type = storage::OperationType::kRead;
  operation.vmo_offset = 0;
  operation.dev_offset = JournalStartBlock(superblock);
  operation.length = JournalBlocks(superblock);
  ASSERT_OK(bcache->RunOperation(operation, &buffer));
  std::unique_ptr<uint8_t[]> expected_buffer(new uint8_t[kMinfsBlockSize]);
  memset(expected_buffer.get(), 0, kMinfsBlockSize);
  for (size_t i = fs::kJournalMetadataBlocks; i < JournalBlocks(superblock); i++) {
    EXPECT_BYTES_EQ(buffer.Data(i), expected_buffer.get(), kMinfsBlockSize);
  }
}

const uint64_t kSliceSize = kMinfsBlockSize * 8;
const uint64_t kSliceCount = 1028;

TEST(FormatFVMFilesystemTest, FVMIncorrectVsliceCount) {
  auto device =
      std::make_unique<FakeFVMBlockDevice>(kBlockCount, kBlockSize, kSliceSize, kSliceCount);
  std::unique_ptr<Bcache> bcache;

  ASSERT_OK(Bcache::Create(std::move(device), kBlockCount, &bcache));
  ASSERT_OK(Mkfs(bcache.get()));

  Superblock sb;
  EXPECT_OK(bcache->Readblk(0, &sb));

  // Expect FVM flag to be set correctly.
  EXPECT_EQ(sb.flags & kMinfsFlagFVM, kMinfsFlagFVM);

  // Expect vslice_count to be aggregate of the slice counts + superblock slice.
  uint32_t expected_vslice_count = CalculateVsliceCount(sb);
  EXPECT_EQ(expected_vslice_count, sb.vslice_count);
}

}  // namespace
}  // namespace minfs
