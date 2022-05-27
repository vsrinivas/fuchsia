// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <gtest/gtest.h>

#include "src/lib/storage/block_client/cpp/fake_block_device.h"
#include "src/storage/minfs/format.h"
#include "src/storage/minfs/minfs_private.h"

namespace minfs {
namespace {

using ::block_client::FakeBlockDevice;

constexpr uint64_t kBlockCount = 1 << 15;
constexpr uint32_t kBlockSize = 512;

TEST(MountTest, OldestRevisionUpdatedOnMount) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
  auto bcache_or = Bcache::Create(std::move(device), kBlockCount);
  ASSERT_TRUE(bcache_or.is_ok());
  ASSERT_TRUE(Mkfs(bcache_or.value().get()).is_ok());
  auto superblock_or = LoadSuperblock(bcache_or.value().get());
  ASSERT_TRUE(superblock_or.is_ok());

  ASSERT_EQ(kMinfsCurrentMinorVersion, superblock_or->oldest_minor_version);

  superblock_or->oldest_minor_version = kMinfsCurrentMinorVersion + 1;
  UpdateChecksum(&superblock_or.value());
  ASSERT_TRUE(bcache_or->Writeblk(kSuperblockStart, &superblock_or.value()).is_ok());
  superblock_or = LoadSuperblock(bcache_or.value().get());
  ASSERT_TRUE(superblock_or.is_ok());
  ASSERT_EQ(kMinfsCurrentMinorVersion + 1, superblock_or->oldest_minor_version);

  MountOptions options = {};
  auto fs_or = Minfs::Create(loop.dispatcher(), std::move(bcache_or.value()), options);
  ASSERT_TRUE(fs_or.is_ok());

  bcache_or = zx::ok(Minfs::Destroy(std::move(fs_or.value())));

  superblock_or = LoadSuperblock(bcache_or.value().get());
  ASSERT_TRUE(superblock_or.is_ok());
  ASSERT_EQ(kMinfsCurrentMinorVersion, superblock_or->oldest_minor_version);
}

TEST(MountTest, ReadsExceptForSuperBlockFail) {
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
  auto* device_ptr = device.get();
  auto bcache_or = Bcache::Create(std::move(device), kBlockCount);
  ASSERT_TRUE(bcache_or.is_ok());
  ASSERT_TRUE(Mkfs(bcache_or.value().get()).is_ok());

  // Fail request for block 8 which should be the first block of the inode bitmap.
  device_ptr->set_hook([](const block_fifo_request_t& request, const zx::vmo*) {
    return request.dev_offset == 8 * kMinfsBlockSize / kBlockSize ? ZX_ERR_IO : ZX_OK;
  });

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto fs_or = Minfs::Create(loop.dispatcher(), std::move(bcache_or.value()), {});
  EXPECT_EQ(fs_or.status_value(), ZX_ERR_IO);
}

}  // namespace
}  // namespace minfs
