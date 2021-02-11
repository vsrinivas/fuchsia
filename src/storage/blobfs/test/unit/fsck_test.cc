// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/fsck.h"

#include <block-client/cpp/fake-device.h>
#include <gtest/gtest.h>

#include "src/storage/blobfs/blobfs.h"
#include "src/storage/blobfs/mkfs.h"
#include "src/storage/blobfs/test/unit/utils.h"

namespace blobfs {
namespace {

using block_client::FakeBlockDevice;

constexpr uint32_t kBlockSize = 512;
constexpr uint32_t kNumBlocks = 400 * kBlobfsBlockSize / kBlockSize;

TEST(FsckTest, TestEmpty) {
  auto device = std::make_unique<FakeBlockDevice>(kNumBlocks, kBlockSize);
  ASSERT_TRUE(device);
  ASSERT_EQ(FormatFilesystem(device.get(), FilesystemOptions{}), ZX_OK);

  ASSERT_EQ(Fsck(std::move(device), MountOptions()), ZX_OK);
}

TEST(FsckTest, TestUnmountable) {
  auto device = std::make_unique<FakeBlockDevice>(kNumBlocks, kBlockSize);
  ASSERT_TRUE(device);

  ASSERT_EQ(Fsck(std::move(device), MountOptions()), ZX_ERR_INVALID_ARGS);
}

TEST(FsckTest, TestCorrupted) {
  auto device = std::make_unique<FakeBlockDevice>(kNumBlocks, kBlockSize);
  ASSERT_TRUE(device);
  ASSERT_EQ(FormatFilesystem(device.get(), FilesystemOptions{}), ZX_OK);

  char block[kBlobfsBlockSize];
  DeviceBlockRead(device.get(), block, sizeof(block), kSuperblockOffset);
  Superblock* info = reinterpret_cast<Superblock*>(block);
  info->alloc_inode_count++;
  DeviceBlockWrite(device.get(), block, sizeof(block), kSuperblockOffset);

  ASSERT_EQ(Fsck(std::move(device), MountOptions()), ZX_ERR_IO_OVERRUN);
}

TEST(FsckTest, TestOverflow) {
  auto device = std::make_unique<FakeBlockDevice>(kNumBlocks, kBlockSize);
  ASSERT_TRUE(device);
  ASSERT_EQ(FormatFilesystem(device.get(), FilesystemOptions{}), ZX_OK);

  char block[kBlobfsBlockSize];
  DeviceBlockRead(device.get(), block, sizeof(block), kSuperblockOffset);
  Superblock* info = reinterpret_cast<Superblock*>(block);
  info->inode_count = std::numeric_limits<uint64_t>::max();
  DeviceBlockWrite(device.get(), block, sizeof(block), kSuperblockOffset);

  ASSERT_EQ(Fsck(std::move(device), MountOptions()), ZX_ERR_OUT_OF_RANGE);
}

TEST(FsckTest, TestBadBackupSuperblock) {
  auto device = std::make_unique<block_client::FakeFVMBlockDevice>(
      400, kBlobfsBlockSize, /*slice_size=*/32768, /*slice_capacity=*/500);
  ASSERT_TRUE(device);
  ASSERT_EQ(FormatFilesystem(device.get(), FilesystemOptions{}), ZX_OK);

  char block[kBlobfsBlockSize];
  memset(block, 0xaf, sizeof(block));
  DeviceBlockWrite(device.get(), block, sizeof(block), kBlobfsBlockSize);

  ASSERT_EQ(Fsck(std::move(device), MountOptions()), ZX_ERR_INVALID_ARGS);
}

TEST(FsckTest, TestNoBackupSuperblockOnOldRevsiionPassesFsck) {
  auto device = std::make_unique<block_client::FakeFVMBlockDevice>(
      400, kBlobfsBlockSize, /*slice_size=*/32768, /*slice_capacity=*/500);
  ASSERT_TRUE(device);
  ASSERT_EQ(FormatFilesystem(device.get(), FilesystemOptions{}), ZX_OK);

  Superblock superblock;
  DeviceBlockRead(device.get(), &superblock, sizeof(superblock), 0);
  superblock.oldest_revision = kBlobfsRevisionBackupSuperblock - 1;
  DeviceBlockWrite(device.get(), &superblock, sizeof(superblock), 0);
  memset(&superblock, 0xaf, sizeof(superblock));
  DeviceBlockWrite(device.get(), &superblock, sizeof(superblock), kBlobfsBlockSize);

  ASSERT_EQ(Fsck(std::move(device), MountOptions{.writability = Writability::ReadOnlyDisk}), ZX_OK);
}

}  // namespace
}  // namespace blobfs
