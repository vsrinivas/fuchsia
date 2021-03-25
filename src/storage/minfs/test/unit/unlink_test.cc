// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <block-client/cpp/fake-device.h>
#include <gtest/gtest.h>

#include "src/storage/minfs/bcache.h"
#include "src/storage/minfs/directory.h"
#include "src/storage/minfs/file.h"
#include "src/storage/minfs/format.h"
#include "src/storage/minfs/minfs_private.h"

namespace minfs {
namespace {

using block_client::FakeBlockDevice;

TEST(UnlinkTest, PurgedFileHasCorrectMagic) {
  constexpr uint64_t kBlockCount = 1 << 20;
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kMinfsBlockSize);

  std::unique_ptr<Bcache> bcache;
  EXPECT_EQ(Bcache::Create(std::move(device), kBlockCount, &bcache), ZX_OK);
  EXPECT_EQ(Mkfs(bcache.get()), ZX_OK);
  MountOptions options = {};

  std::unique_ptr<Minfs> fs;
  EXPECT_EQ(Minfs::Create(std::move(bcache), options, &fs), ZX_OK);

  ino_t ino;
  uint32_t inode_block;
  {
    fbl::RefPtr<VnodeMinfs> root;
    EXPECT_EQ(fs->VnodeGet(&root, kMinfsRootIno), ZX_OK);
    fbl::RefPtr<fs::Vnode> fs_child;
    EXPECT_EQ(root->Create("foo", 0, &fs_child), ZX_OK);
    auto child = fbl::RefPtr<File>::Downcast(std::move(fs_child));

    ino = child->GetIno();
    EXPECT_EQ(child->Close(), ZX_OK);
    EXPECT_EQ(root->Unlink("foo", /*must_be_dir=*/false), ZX_OK);
    inode_block = fs->Info().ino_block + ino / kMinfsInodesPerBlock;
  }
  bcache = Minfs::Destroy(std::move(fs));

  Inode inodes[kMinfsInodesPerBlock];
  EXPECT_EQ(bcache->Readblk(inode_block, &inodes), ZX_OK);

  EXPECT_EQ(inodes[ino % kMinfsInodesPerBlock].magic, kMinfsMagicPurged);
}

TEST(UnlinkTest, UnlinkedDirectoryFailure) {
  constexpr uint64_t kBlockCount = 1 << 20;
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kMinfsBlockSize);

  std::unique_ptr<Bcache> bcache;
  EXPECT_EQ(Bcache::Create(std::move(device), kBlockCount, &bcache), ZX_OK);
  EXPECT_EQ(Mkfs(bcache.get()), ZX_OK);
  MountOptions options = {};

  std::unique_ptr<Minfs> fs;
  EXPECT_EQ(Minfs::Create(std::move(bcache), options, &fs), ZX_OK);

  {
    fbl::RefPtr<VnodeMinfs> root;
    EXPECT_EQ(fs->VnodeGet(&root, kMinfsRootIno), ZX_OK);
    fbl::RefPtr<fs::Vnode> fs_child;
    EXPECT_EQ(root->Create("foo", S_IFDIR, &fs_child), ZX_OK);
    EXPECT_EQ(root->Unlink("foo", true), ZX_OK);
    auto child = fbl::RefPtr<Directory>::Downcast(std::move(fs_child));
    EXPECT_EQ(0ul, child->GetInode()->size);
    EXPECT_EQ(child->Unlink("bar", false), ZX_ERR_NOT_FOUND);
    EXPECT_EQ(child->Rename(root, "bar", "bar", false, false), ZX_ERR_NOT_FOUND);
    fbl::RefPtr<fs::Vnode> unused_child;
    EXPECT_EQ(child->Lookup("bar", &unused_child), ZX_ERR_NOT_FOUND);
    EXPECT_EQ(child->Close(), ZX_OK);
  }
  bcache = Minfs::Destroy(std::move(fs));
}

}  // namespace
}  // namespace minfs
