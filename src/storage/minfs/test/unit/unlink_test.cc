// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <block-client/cpp/fake-device.h>
#include <zxtest/zxtest.h>

#include "src/storage/minfs/bcache.h"
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
  EXPECT_OK(Bcache::Create(std::move(device), kBlockCount, &bcache));
  EXPECT_OK(Mkfs(bcache.get()));
  MountOptions options = {};

  std::unique_ptr<Minfs> fs;
  EXPECT_OK(Minfs::Create(std::move(bcache), options, &fs));

  ino_t ino;
  uint32_t inode_block;
  {
    fbl::RefPtr<VnodeMinfs> root;
    EXPECT_OK(fs->VnodeGet(&root, kMinfsRootIno));
    fbl::RefPtr<fs::Vnode> fs_child;
    EXPECT_OK(root->Create("foo", 0, &fs_child));
    auto child = fbl::RefPtr<File>::Downcast(std::move(fs_child));

    ino = child->GetIno();
    EXPECT_OK(child->Close());
    EXPECT_OK(root->Unlink("foo", /*must_be_dir=*/false));
    inode_block = fs->Info().ino_block + ino / kMinfsInodesPerBlock;
  }
  bcache = Minfs::Destroy(std::move(fs));

  Inode inodes[kMinfsInodesPerBlock];
  EXPECT_OK(bcache->Readblk(inode_block, &inodes));

  EXPECT_EQ(inodes[ino % kMinfsInodesPerBlock].magic, kMinfsMagicPurged);
}

}  // namespace
}  // namespace minfs
