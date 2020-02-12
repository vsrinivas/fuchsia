// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "blobfs/fsck.h"

#include <blobfs/mkfs.h>
#include <block-client/cpp/fake-device.h>
#include <zxtest/zxtest.h>

#include "blobfs.h"
#include "utils.h"

namespace blobfs {
namespace {

using block_client::FakeBlockDevice;

constexpr uint32_t kBlockSize = 512;
constexpr uint32_t kNumBlocks = 400 * kBlobfsBlockSize / kBlockSize;

TEST(FsckTest, TestEmpty) {
    auto device = std::make_unique<FakeBlockDevice>(kNumBlocks, kBlockSize);
    ASSERT_TRUE(device);
    ASSERT_OK(FormatFilesystem(device.get()));

    MountOptions options;
    ASSERT_OK(Fsck(std::move(device), &options));
}

TEST(FsckTest, TestUnmountable) {
    auto device = std::make_unique<FakeBlockDevice>(kNumBlocks, kBlockSize);
    ASSERT_TRUE(device);

    MountOptions options;
    ASSERT_STATUS(Fsck(std::move(device), &options), ZX_ERR_INVALID_ARGS);
}

TEST(FsckTest, TestCorrupted) {
    auto device = std::make_unique<FakeBlockDevice>(kNumBlocks, kBlockSize);
    ASSERT_TRUE(device);
    ASSERT_OK(FormatFilesystem(device.get()));

    char block[kBlobfsBlockSize];
    DeviceBlockRead(device.get(), block, sizeof(block), kSuperblockOffset);
    Superblock* info = reinterpret_cast<Superblock*>(block);
    info->alloc_inode_count++;
    DeviceBlockWrite(device.get(), block, sizeof(block), kSuperblockOffset);

    MountOptions options;
    ASSERT_STATUS(Fsck(std::move(device), &options), ZX_ERR_IO_OVERRUN);
}

}  // namespace
}  // namespace blobfs
