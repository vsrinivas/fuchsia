// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <block-client/cpp/fake-device.h>
#include <minfs/format.h>
#include <zxtest/zxtest.h>

#include "minfs_private.h"

namespace minfs {
namespace {

using block_client::FakeBlockDevice;

constexpr uint64_t kBlockCount = 1 << 15;
constexpr uint32_t kBlockSize = 512;

TEST(MountTest, OldestRevisionUpdatedOnMount) {
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
  std::unique_ptr<Bcache> bcache;
  ASSERT_OK(Bcache::Create(std::move(device), kBlockCount, &bcache));
  ASSERT_OK(Mkfs(bcache.get()));
  Superblock superblock = {};
  ASSERT_OK(LoadSuperblock(bcache.get(), &superblock));

  ASSERT_EQ(kMinfsRevision, superblock.oldest_revision);

  superblock.oldest_revision = kMinfsRevision + 1;
  UpdateChecksum(&superblock);
  ASSERT_OK(bcache->Writeblk(kSuperblockStart, &superblock));
  ASSERT_OK(LoadSuperblock(bcache.get(), &superblock));
  ASSERT_EQ(kMinfsRevision + 1, superblock.oldest_revision);

  MountOptions options = {};
  std::unique_ptr<Minfs> fs;
  ASSERT_OK(Minfs::Create(std::move(bcache), options, &fs));
  bcache = Minfs::Destroy(std::move(fs));

  ASSERT_OK(LoadSuperblock(bcache.get(), &superblock));
  ASSERT_EQ(kMinfsRevision, superblock.oldest_revision);
}

}  // namespace
}  // namespace minfs
