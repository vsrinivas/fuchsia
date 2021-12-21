// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/async-loop/default.h>
#include <limits.h>
#include <stdint.h>
#include <unistd.h>

#include <climits>

#include <zxtest/zxtest.h>

#include "src/lib/storage/block_client/cpp/fake_block_device.h"
#include "src/lib/storage/vfs/cpp/managed_vfs.h"
#include "src/storage/factory/factoryfs/factoryfs.h"
#include "src/storage/factory/factoryfs/format.h"
#include "src/storage/factory/factoryfs/mkfs.h"
#include "utils.h"

using block_client::FakeBlockDevice;

namespace factoryfs {
namespace {
constexpr uint64_t kBlockCount = 1024;

void CreateAndFormatDevice(std::unique_ptr<FakeBlockDevice>* out) {
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kFactoryfsBlockSize);
  ASSERT_OK(FormatFilesystem(device.get()));

  *out = std::move(device);
}

TEST(CreateTest, ValidSuperblock) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  std::unique_ptr<FakeBlockDevice> device;
  CreateAndFormatDevice(&device);

  auto vfs = std::make_unique<fs::ManagedVfs>(loop.dispatcher());

  factoryfs::MountOptions options;
  auto fs_or = Factoryfs::Create(nullptr, std::move(device), &options, vfs.get());
  ASSERT_TRUE(fs_or.is_ok());
}

TEST(CreateTest, InvalidSuperblock) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  std::unique_ptr<FakeBlockDevice> device;
  CreateAndFormatDevice(&device);

  char block[kFactoryfsBlockSize];
  DeviceBlockRead(device.get(), block, sizeof(block), kSuperblockStart);
  Superblock* info = reinterpret_cast<Superblock*>(block);
  info->magic++;
  DeviceBlockWrite(device.get(), block, sizeof(block), kSuperblockStart);

  auto vfs = std::make_unique<fs::ManagedVfs>(loop.dispatcher());

  factoryfs::MountOptions options;
  auto fs_or = Factoryfs::Create(nullptr, std::move(device), &options, vfs.get());
  ASSERT_TRUE(fs_or.is_error());
  EXPECT_EQ(ZX_ERR_IO_DATA_INTEGRITY, fs_or.error_value());
}
}  // namespace

}  // namespace factoryfs
