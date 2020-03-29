// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/sync/completion.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <block-client/cpp/fake-device.h>
#include <fs-management/mount.h>
#include <minfs/format.h>
#include <minfs/fsck.h>
#include <safemath/checked_math.h>
#include <zxtest/zxtest.h>

#include "minfs-private.h"

namespace minfs {
namespace {

using block_client::FakeBlockDevice;

constexpr uint64_t kBlockCount = 1 << 20;
constexpr uint32_t kBlockSize = 512;

class ConsistencyCheckerFixture : public zxtest::Test {
 public:
  void SetUp() override { device_ = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize); }

  std::unique_ptr<FakeBlockDevice> take_device() { return std::move(device_); }

 private:
  std::unique_ptr<FakeBlockDevice> device_;
};

using ConsistencyCheckerTest = ConsistencyCheckerFixture;

TEST_F(ConsistencyCheckerTest, NewlyFormattedFilesystemWithRepair) {
  auto device = take_device();
  std::unique_ptr<Bcache> bcache;
  ASSERT_OK(Bcache::Create(std::move(device), kBlockCount, &bcache));
  ASSERT_OK(Mkfs(bcache.get()));
  ASSERT_OK(Fsck(std::move(bcache), Repair::kEnabled));
}

TEST_F(ConsistencyCheckerTest, NewlyFormattedFilesystemWithoutRepair) {
  auto device = take_device();
  std::unique_ptr<Bcache> bcache;
  ASSERT_OK(Bcache::Create(std::move(device), kBlockCount, &bcache));
  ASSERT_OK(Mkfs(bcache.get()));
  ASSERT_OK(Fsck(std::move(bcache), Repair::kDisabled));
}

TEST_F(ConsistencyCheckerTest, NewlyFormattedFilesystemCheckAfterMount) {
  auto device = take_device();
  std::unique_ptr<Bcache> bcache;
  ASSERT_OK(Bcache::Create(std::move(device), kBlockCount, &bcache));
  ASSERT_OK(Mkfs(bcache.get()));

  MountOptions options = {};
  std::unique_ptr<Minfs> fs;
  ASSERT_OK(Minfs::Create(std::move(bcache), options, &fs));
  bcache = Minfs::Destroy(std::move(fs));
  ASSERT_OK(Fsck(std::move(bcache), Repair::kEnabled));
}

class ConsistencyCheckerFixtureVerbose : public zxtest::Test {
 public:
  void SetUp() override {
    auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kMinfsBlockSize);

    std::unique_ptr<Bcache> bcache;
    EXPECT_OK(Bcache::Create(std::move(device), kBlockCount, &bcache));
    EXPECT_OK(Mkfs(bcache.get()));
    MountOptions options = {};

    EXPECT_OK(Minfs::Create(std::move(bcache), options, &fs_));
  }

  Minfs* get_fs() const { return fs_.get(); }

  void destroy_fs(std::unique_ptr<Bcache>* bcache) {
    sync_completion_t completion;
    fs_->Sync([&completion](zx_status_t status) { sync_completion_signal(&completion); });
    EXPECT_OK(sync_completion_wait(&completion, zx::duration::infinite().get()));
    *bcache = Minfs::Destroy(std::move(fs_));
  }

  fs::VnodeAttributes CreateAndWrite(const char* name, size_t truncate_size, size_t offset,
                                     size_t data_size) {
    fbl::RefPtr<VnodeMinfs> root;
    EXPECT_OK(fs_->VnodeGet(&root, kMinfsRootIno));
    fbl::RefPtr<fs::Vnode> child;
    EXPECT_OK(root->Create(&child, name, 0));
    if (data_size != 0) {
      char data[data_size];
      memset(data, 0, data_size);
      size_t size_written;
      EXPECT_OK(child->Write(data, data_size, offset, &size_written));
      EXPECT_EQ(size_written, data_size);
    }
    if (truncate_size > 0) {
      EXPECT_OK(child->Truncate(truncate_size));
    }
    fs::VnodeAttributes stat;
    EXPECT_OK(child->GetAttributes(&stat));
    EXPECT_OK(child->Close());
    return stat;
  }

  void TearDown() override { EXPECT_NULL(fs_.get()); }

 private:
  std::unique_ptr<Minfs> fs_;
};

TEST_F(ConsistencyCheckerFixtureVerbose, TwoInodesPointToABlock) {
  fs::VnodeAttributes file1_stat = {}, file2_stat = {};
  {
    // Create a file with one data block.
    file1_stat = CreateAndWrite("file1", 0, 0, kMinfsBlockSize);
  }

  {
    // Create an empty file.
    file2_stat = CreateAndWrite("file2", 0, 0, 0);
  }

  EXPECT_NE(file1_stat.inode, file2_stat.inode);

  // To keep test simple, we ensure here that inodes allocated for file1 and
  // file2 are within the same block in the inode table.
  EXPECT_EQ(file1_stat.inode / kMinfsInodesPerBlock, file2_stat.inode / kMinfsInodesPerBlock);

  std::unique_ptr<Bcache> bcache;
  destroy_fs(&bcache);

  Superblock sb;
  EXPECT_OK(bcache->Readblk(0, &sb));

  Inode inodes[kMinfsInodesPerBlock];
  blk_t inode_block =
      safemath::checked_cast<uint32_t>(sb.ino_block + (file1_stat.inode / kMinfsInodesPerBlock));
  EXPECT_OK(bcache->Readblk(inode_block, &inodes));

  size_t file1_ino = file1_stat.inode % kMinfsInodesPerBlock;
  size_t file2_ino = file2_stat.inode % kMinfsInodesPerBlock;

  // The test code has hard dependency on filesystem layout.
  // TODO(fxb/39741): Isolate this test from the on-disk format.
  EXPECT_GT(inodes[file1_ino].dnum[0], 0);
  EXPECT_EQ(inodes[file2_ino].dnum[0], 0);

  // Make second file to point to the block owned by first file.
  inodes[file2_ino].dnum[0] = inodes[file1_ino].dnum[0];
  inodes[file2_ino].block_count = inodes[file1_ino].block_count;
  inodes[file2_ino].size = inodes[file1_ino].size;
  EXPECT_OK(bcache->Writeblk(inode_block, inodes));

  ASSERT_NOT_OK(Fsck(std::move(bcache), Repair::kEnabled, &bcache));
}

TEST_F(ConsistencyCheckerFixtureVerbose, TwoOffsetsPointToABlock) {
  fs::VnodeAttributes file_stat = {};
  file_stat = CreateAndWrite("file", 2 * kMinfsBlockSize, 0, kMinfsBlockSize);

  std::unique_ptr<Bcache> bcache;
  destroy_fs(&bcache);

  Superblock sb;
  EXPECT_OK(bcache->Readblk(0, &sb));

  Inode inodes[kMinfsInodesPerBlock];
  blk_t inode_block =
      safemath::checked_cast<uint32_t>(sb.ino_block + (file_stat.inode / kMinfsInodesPerBlock));
  EXPECT_OK(bcache->Readblk(inode_block, &inodes));

  size_t file_ino = file_stat.inode % kMinfsInodesPerBlock;

  EXPECT_GT(inodes[file_ino].dnum[0], 0);
  EXPECT_EQ(inodes[file_ino].dnum[1], 0);

  // Make second block offset point to the first block.
  inodes[file_ino].dnum[1] = inodes[file_ino].dnum[0];
  EXPECT_OK(bcache->Writeblk(inode_block, inodes));

  ASSERT_NOT_OK(Fsck(std::move(bcache), Repair::kEnabled, &bcache));
}

TEST_F(ConsistencyCheckerFixtureVerbose, IndirectBlocksShared) {
  fs::VnodeAttributes file_stat = {};
  uint64_t double_indirect_offset =
      (kMinfsDirect + (kMinfsIndirect * kMinfsDirectPerIndirect) + 1) * kMinfsBlockSize;
  file_stat = CreateAndWrite("file", double_indirect_offset, 0, kMinfsBlockSize);

  std::unique_ptr<Bcache> bcache;
  destroy_fs(&bcache);

  Superblock sb;
  EXPECT_OK(bcache->Readblk(0, &sb));

  Inode inodes[kMinfsInodesPerBlock];
  blk_t inode_block =
      safemath::checked_cast<uint32_t>(sb.ino_block + (file_stat.inode / kMinfsInodesPerBlock));
  EXPECT_OK(bcache->Readblk(inode_block, &inodes));

  size_t file_ino = file_stat.inode % kMinfsInodesPerBlock;

  EXPECT_GT(inodes[file_ino].dnum[0], 0);
  EXPECT_EQ(inodes[file_ino].dnum[1], 0);
  EXPECT_EQ(inodes[file_ino].inum[0], 0);
  EXPECT_EQ(inodes[file_ino].dinum[0], 0);

  // Make various indirect blocks to point to the data block.
  inodes[file_ino].dnum[1] = inodes[file_ino].dnum[0];
  inodes[file_ino].inum[0] = inodes[file_ino].dnum[0];
  inodes[file_ino].dinum[0] = inodes[file_ino].dnum[0];
  EXPECT_OK(bcache->Writeblk(inode_block, inodes));

  ASSERT_NOT_OK(Fsck(std::move(bcache), Repair::kEnabled, &bcache));
}

TEST_F(ConsistencyCheckerFixtureVerbose, PurgedFileWithBadMagic) {
  std::unique_ptr<Bcache> bcache;
  destroy_fs(&bcache);

  Superblock sb;
  EXPECT_OK(bcache->Readblk(0, &sb));

  Inode inodes[kMinfsInodesPerBlock];
  EXPECT_OK(bcache->Readblk(sb.ino_block, &inodes));
  inodes[31].magic = kMinfsMagicFile;
  EXPECT_OK(bcache->Writeblk(sb.ino_block, inodes));

  ASSERT_NOT_OK(Fsck(std::move(bcache), Repair::kEnabled, &bcache));
}

}  // namespace
}  // namespace minfs
