// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <unistd.h>

#include <climits>

#include <gtest/gtest.h>

#include "src/lib/storage/block_client/cpp/fake_block_device.h"
#include "src/storage/blobfs/blobfs.h"
#include "src/storage/blobfs/format.h"
#include "src/storage/blobfs/mkfs.h"
#include "src/storage/blobfs/test/blobfs_test_setup.h"
#include "src/storage/blobfs/test/unit/utils.h"

namespace blobfs {
namespace {

TEST(CreateTest, AllocNodeCountGreaterThanAllocated) {
  constexpr uint64_t kBlockCount = 1024;
  auto device = std::make_unique<block_client::FakeBlockDevice>(kBlockCount, kBlobfsBlockSize);
  ASSERT_EQ(FormatFilesystem(device.get(), FilesystemOptions()), ZX_OK);

  char block[kBlobfsBlockSize];
  DeviceBlockRead(device.get(), block, sizeof(block), kSuperblockOffset);
  Superblock* info = reinterpret_cast<Superblock*>(block);
  info->alloc_inode_count++;
  DeviceBlockWrite(device.get(), block, sizeof(block), kSuperblockOffset);

  BlobfsTestSetup setup;
  EXPECT_EQ(ZX_ERR_IO_OVERRUN, setup.Mount(std::move(device)));
}

}  // namespace

}  // namespace blobfs
