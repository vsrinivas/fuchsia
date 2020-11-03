// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <block-client/cpp/fake-device.h>
#include <zxtest/zxtest.h>

#include "src/storage/minfs/format.h"
#include "src/storage/minfs/fsck.h"
#include "src/storage/minfs/minfs_private.h"

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

  // Verify the superblock has the correct versions.
  Superblock new_superblock = {};
  ASSERT_OK(LoadSuperblock(bcache.get(), &new_superblock));
  EXPECT_EQ(kMinfsCurrentFormatVersion, new_superblock.format_version);
  EXPECT_EQ(kMinfsCurrentRevision, new_superblock.oldest_revision);

  // Verify that the device has written zeros to the expected location, overwriting the sentinel
  // pages.
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

}  // namespace
}  // namespace minfs
