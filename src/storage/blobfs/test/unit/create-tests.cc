// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <unistd.h>

#include <climits>

#include <blobfs/format.h>
#include <blobfs/mkfs.h>
#include <block-client/cpp/fake-device.h>
#include <zxtest/zxtest.h>

#include "blobfs.h"
#include "utils.h"

namespace blobfs {
namespace {
constexpr uint64_t kBlockCount = 1024;

using block_client::FakeBlockDevice;

void CreateAndFormatDevice(std::unique_ptr<FakeBlockDevice>* out) {
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlobfsBlockSize);
  ASSERT_OK(FormatFilesystem(device.get()));

  *out = std::move(device);
}

TEST(CreateTest, ValidSuperblock) {
  std::unique_ptr<FakeBlockDevice> device;
  CreateAndFormatDevice(&device);

  std::unique_ptr<blobfs::Blobfs> blobfs;
  blobfs::MountOptions options;
  EXPECT_OK(Blobfs::Create(nullptr, std::move(device), &options, zx::resource(), &blobfs));
  EXPECT_NOT_NULL(blobfs.get());
}

TEST(CreateTest, AllocNodeCountGreaterThanAllocated) {
  std::unique_ptr<FakeBlockDevice> device;
  CreateAndFormatDevice(&device);

  char block[kBlobfsBlockSize];
  DeviceBlockRead(device.get(), block, sizeof(block), kSuperblockOffset);
  Superblock* info = reinterpret_cast<Superblock*>(block);
  info->alloc_inode_count++;
  DeviceBlockWrite(device.get(), block, sizeof(block), kSuperblockOffset);

  std::unique_ptr<blobfs::Blobfs> blobfs;
  blobfs::MountOptions options;

  EXPECT_EQ(Blobfs::Create(nullptr, std::move(device), &options, zx::resource(), &blobfs),
            ZX_ERR_IO_OVERRUN);
  EXPECT_NULL(blobfs.get());
}

}  // namespace

}  // namespace blobfs
