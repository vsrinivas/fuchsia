// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <unistd.h>

#include <climits>

#include <block-client/cpp/fake-device.h>
#include <gtest/gtest.h>

#include "src/storage/blobfs/blobfs.h"
#include "src/storage/blobfs/format.h"
#include "src/storage/blobfs/mkfs.h"
#include "src/storage/blobfs/test/unit/utils.h"

namespace blobfs {
namespace {
constexpr uint64_t kBlockCount = 1024;

using block_client::FakeBlockDevice;

void CreateAndFormatDevice(std::unique_ptr<FakeBlockDevice>* out) {
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlobfsBlockSize);
  ASSERT_EQ(FormatFilesystem(device.get(), FilesystemOptions{}), ZX_OK);

  *out = std::move(device);
}

TEST(CreateTest, ValidSuperblock) {
  std::unique_ptr<FakeBlockDevice> device;
  CreateAndFormatDevice(&device);

  auto blobfs_or = Blobfs::Create(nullptr, std::move(device));
  EXPECT_TRUE(blobfs_or.is_ok());
  EXPECT_TRUE(*blobfs_or);
}

TEST(CreateTest, AllocNodeCountGreaterThanAllocated) {
  std::unique_ptr<FakeBlockDevice> device;
  CreateAndFormatDevice(&device);

  char block[kBlobfsBlockSize];
  DeviceBlockRead(device.get(), block, sizeof(block), kSuperblockOffset);
  Superblock* info = reinterpret_cast<Superblock*>(block);
  info->alloc_inode_count++;
  DeviceBlockWrite(device.get(), block, sizeof(block), kSuperblockOffset);

  auto blobfs_or = Blobfs::Create(nullptr, std::move(device));
  EXPECT_EQ(blobfs_or.status_value(), ZX_ERR_IO_OVERRUN);
}

}  // namespace

}  // namespace blobfs
