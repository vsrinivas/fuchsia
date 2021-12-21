// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <gtest/gtest.h>

#include "src/lib/storage/block_client/cpp/fake_block_device.h"
#include "src/storage/minfs/bcache.h"
#include "src/storage/minfs/directory.h"
#include "src/storage/minfs/file.h"
#include "src/storage/minfs/format.h"
#include "src/storage/minfs/minfs_private.h"

namespace minfs {
namespace {

using block_client::FakeBlockDevice;

TEST(UnlinkTest, PurgedFileHasCorrectMagic) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  constexpr uint64_t kBlockCount = 1 << 20;
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kMinfsBlockSize);

  auto bcache_or = Bcache::Create(std::move(device), kBlockCount);
  EXPECT_TRUE(bcache_or.is_ok());
  EXPECT_TRUE(Mkfs(bcache_or.value().get()).is_ok());
  MountOptions options = {};

  auto fs_or = Minfs::Create(loop.dispatcher(), std::move(bcache_or.value()), options);
  EXPECT_TRUE(fs_or.is_ok());

  ino_t ino;
  uint32_t inode_block;
  {
    auto root_or = fs_or->VnodeGet(kMinfsRootIno);
    EXPECT_TRUE(root_or.is_ok());
    fbl::RefPtr<fs::Vnode> fs_child;
    EXPECT_EQ(root_or->Create("foo", 0, &fs_child), ZX_OK);
    auto child = fbl::RefPtr<File>::Downcast(std::move(fs_child));

    ino = child->GetIno();
    EXPECT_EQ(child->Close(), ZX_OK);
    EXPECT_EQ(root_or->Unlink("foo", /*must_be_dir=*/false), ZX_OK);
    inode_block = fs_or->Info().ino_block + ino / kMinfsInodesPerBlock;
  }
  bcache_or = zx::ok(Minfs::Destroy(std::move(fs_or.value())));

  Inode inodes[kMinfsInodesPerBlock];
  EXPECT_TRUE(bcache_or->Readblk(inode_block, &inodes).is_ok());

  EXPECT_EQ(inodes[ino % kMinfsInodesPerBlock].magic, kMinfsMagicPurged);
}

TEST(UnlinkTest, UnlinkedDirectoryFailure) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  constexpr uint64_t kBlockCount = 1 << 20;
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kMinfsBlockSize);

  auto bcache_or = Bcache::Create(std::move(device), kBlockCount);
  EXPECT_TRUE(bcache_or.is_ok());
  EXPECT_TRUE(Mkfs(bcache_or.value().get()).is_ok());
  MountOptions options = {};

  auto fs_or = Minfs::Create(loop.dispatcher(), std::move(bcache_or.value()), options);
  EXPECT_TRUE(fs_or.is_ok());

  {
    auto root_or = fs_or->VnodeGet(kMinfsRootIno);
    EXPECT_TRUE(root_or.is_ok());
    fbl::RefPtr<fs::Vnode> fs_child;
    EXPECT_EQ(root_or->Create("foo", S_IFDIR, &fs_child), ZX_OK);
    EXPECT_EQ(root_or->Unlink("foo", true), ZX_OK);
    auto child = fbl::RefPtr<Directory>::Downcast(std::move(fs_child));
    EXPECT_EQ(0ul, child->GetInode()->size);
    EXPECT_EQ(child->Unlink("bar", false), ZX_ERR_NOT_FOUND);
    EXPECT_EQ(child->Rename(root_or.value(), "bar", "bar", false, false), ZX_ERR_NOT_FOUND);
    fbl::RefPtr<fs::Vnode> unused_child;
    EXPECT_EQ(child->Lookup("bar", &unused_child), ZX_ERR_NOT_FOUND);
    EXPECT_EQ(child->Close(), ZX_OK);
  }

  [[maybe_unused]] auto bcache = Minfs::Destroy(std::move(fs_or.value()));
}

}  // namespace
}  // namespace minfs
