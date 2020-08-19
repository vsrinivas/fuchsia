// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <unistd.h>

#include <climits>

#include <block-client/cpp/fake-device.h>
#include <zxtest/zxtest.h>

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
  std::unique_ptr<FakeBlockDevice> device;
  CreateAndFormatDevice(&device);

  std::unique_ptr<factoryfs::Factoryfs> factoryfs;
  factoryfs::MountOptions options;
  EXPECT_OK(Factoryfs::Create(nullptr, std::move(device), &options, &factoryfs));
  EXPECT_NOT_NULL(factoryfs.get());
}

TEST(CreateTest, InvalidSuperblock) {
  std::unique_ptr<FakeBlockDevice> device;
  CreateAndFormatDevice(&device);

  char block[kFactoryfsBlockSize];
  DeviceBlockRead(device.get(), block, sizeof(block), kSuperblockStart);
  Superblock* info = reinterpret_cast<Superblock*>(block);
  info->magic++;
  DeviceBlockWrite(device.get(), block, sizeof(block), kSuperblockStart);

  std::unique_ptr<factoryfs::Factoryfs> factoryfs;
  factoryfs::MountOptions options;

  EXPECT_EQ(Factoryfs::Create(nullptr, std::move(device), &options, &factoryfs),
            ZX_ERR_IO_DATA_INTEGRITY);
  EXPECT_NULL(factoryfs.get());
}
}  // namespace

}  // namespace factoryfs
