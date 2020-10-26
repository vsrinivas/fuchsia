// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/fsck.h"

#include <fcntl.h>
#include <lib/sync/completion.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <block-client/cpp/fake-device.h>
#include <fbl/auto_call.h>
#include <fs-management/mount.h>
#include <fs/journal/format.h>
#include <safemath/checked_math.h>
#include <zxtest/zxtest.h>

#include "src/storage/minfs/format.h"
#include "src/storage/minfs/minfs_private.h"

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
  ASSERT_OK(Fsck(std::move(bcache), FsckOptions{.repair = true}));
}

TEST_F(ConsistencyCheckerTest, NewlyFormattedFilesystemWithoutRepair) {
  auto device = take_device();
  std::unique_ptr<Bcache> bcache;
  ASSERT_OK(Bcache::Create(std::move(device), kBlockCount, &bcache));
  ASSERT_OK(Mkfs(bcache.get()));
  ASSERT_OK(Fsck(std::move(bcache), FsckOptions()));
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
  ASSERT_OK(Fsck(std::move(bcache), FsckOptions{.repair = true}));
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
    EXPECT_OK(root->Create(name, 0, &child));
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
  Minfs& fs() { return *fs_; }
  std::unique_ptr<Minfs> TakeFs() { return std::move(fs_); }

  void MarkDirectoryEntryMissing(size_t offset, std::unique_ptr<Bcache>* bcache);

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
  // TODO(fxbug.dev/39741): Isolate this test from the on-disk format.
  EXPECT_GT(inodes[file1_ino].dnum[0], 0);
  EXPECT_EQ(inodes[file2_ino].dnum[0], 0);

  // Make second file to point to the block owned by first file.
  inodes[file2_ino].dnum[0] = inodes[file1_ino].dnum[0];
  inodes[file2_ino].block_count = inodes[file1_ino].block_count;
  inodes[file2_ino].size = inodes[file1_ino].size;
  EXPECT_OK(bcache->Writeblk(inode_block, inodes));

  ASSERT_NOT_OK(Fsck(std::move(bcache), FsckOptions{.repair = true}, &bcache));
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

  ASSERT_NOT_OK(Fsck(std::move(bcache), FsckOptions{.repair = true}, &bcache));
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

  ASSERT_NOT_OK(Fsck(std::move(bcache), FsckOptions{.repair = true}, &bcache));
}

void ConsistencyCheckerFixtureVerbose::MarkDirectoryEntryMissing(size_t offset,
                                                                 std::unique_ptr<Bcache>* bcache) {
  blk_t root_dir_block;
  {
    fbl::RefPtr<VnodeMinfs> root;
    EXPECT_OK(fs_->VnodeGet(&root, kMinfsRootIno));
    root_dir_block = root->GetInode()->dnum[0] + fs().Info().dat_block;
  }

  destroy_fs(bcache);

  // Need this buffer to be a full block.
  DirentBuffer<kMinfsBlockSize> dirent_buffer;

  ASSERT_OK((*bcache)->Readblk(root_dir_block, dirent_buffer.raw));
  dirent_buffer.dirent.ino = 0;
  ASSERT_OK((*bcache)->Writeblk(root_dir_block, dirent_buffer.raw));
}

TEST_F(ConsistencyCheckerFixtureVerbose, MissingDotEntry) {
  std::unique_ptr<Bcache> bcache;
  MarkDirectoryEntryMissing(0, &bcache);

  ASSERT_NOT_OK(Fsck(std::move(bcache), FsckOptions{.repair = true}, &bcache));
}

TEST_F(ConsistencyCheckerFixtureVerbose, MissingDotDotEntry) {
  std::unique_ptr<Bcache> bcache;
  MarkDirectoryEntryMissing(DirentSize(1), &bcache);

  ASSERT_NOT_OK(Fsck(std::move(bcache), FsckOptions{.repair = true}, &bcache));
}

void CreateUnlinkedDirectoryWithEntry(std::unique_ptr<Minfs> fs,
                                      std::unique_ptr<Bcache>* bcache_out) {
  ino_t ino;
  blk_t inode_block;

  {
    fbl::RefPtr<VnodeMinfs> root;
    ASSERT_OK(fs->VnodeGet(&root, kMinfsRootIno));
    fbl::RefPtr<fs::Vnode> child_;
    ASSERT_OK(root->Create("foo", 0, &child_));
    auto child = fbl::RefPtr<VnodeMinfs>::Downcast(std::move(child_));
    auto close_child = fbl::MakeAutoCall([child]() { child->Close(); });
    ino = child->GetIno();
    ASSERT_GT(kMinfsInodesPerBlock, ino);

    // Need this buffer to be a full block.
    DirentBuffer<kMinfsBlockSize> dirent_buffer;

    uint8_t data[kMinfsBlockSize];
    dirent_buffer.dirent.ino = ino;
    dirent_buffer.dirent.reclen = DirentSize(1);
    dirent_buffer.dirent.namelen = 1;
    dirent_buffer.dirent.type = kMinfsTypeDir;
    dirent_buffer.dirent.name[0] = '.';

    size_t written;
    ASSERT_OK(child->Write(data, dirent_buffer.dirent.reclen, 0, &written));
    ASSERT_EQ(written, dirent_buffer.dirent.reclen);

    ASSERT_OK(root->Unlink("foo", false));

    sync_completion_t completion;
    fs->Sync([&completion](zx_status_t status) { sync_completion_signal(&completion); });
    EXPECT_OK(sync_completion_wait(&completion, zx::duration::infinite().get()));

    inode_block = fs->Info().ino_block;

    // Prevent the inode from being purged when we close the child.
    fs->SetReadonly(true);
    fs->StopWriteback();
  }

  std::unique_ptr<Bcache> bcache = Minfs::Destroy(std::move(fs));

  // Now hack the inode so it looks like a directory with an invalid entry count.
  Inode inodes[kMinfsInodesPerBlock];
  ASSERT_OK(bcache->Readblk(inode_block, &inodes));
  Inode& inode = inodes[ino];
  inode.magic = kMinfsMagicDir;
  inode.dirent_count = 1;
  ASSERT_OK(bcache->Writeblk(inode_block, &inodes));

  *bcache_out = std::move(bcache);
}

TEST_F(ConsistencyCheckerFixtureVerbose, UnlinkedDirectoryHasBadEntryCount) {
  std::unique_ptr<Bcache> bcache;
  ASSERT_NO_FATAL_FAILURES(CreateUnlinkedDirectoryWithEntry(TakeFs(), &bcache));
  ASSERT_NOT_OK(Fsck(std::move(bcache), FsckOptions{.repair = false, .read_only = true}, &bcache));
}

TEST_F(ConsistencyCheckerFixtureVerbose, CorruptSuperblock) {
  std::unique_ptr<Bcache> bcache;
  destroy_fs(&bcache);

  Superblock sb;
  EXPECT_OK(bcache->Readblk(0, &sb));

  // Check if superblock magic is valid
  EXPECT_EQ(sb.magic0, kMinfsMagic0);
  EXPECT_EQ(sb.magic1, kMinfsMagic1);

  // Corrupt the superblock
  sb.checksum = 0;
  EXPECT_OK(bcache->Writeblk(0, &sb));

  ASSERT_NOT_OK(Fsck(std::move(bcache), FsckOptions{.repair = false}, &bcache));
}

TEST_F(ConsistencyCheckerFixtureVerbose, CorruptJournalInfo) {
  std::unique_ptr<Bcache> bcache;
  destroy_fs(&bcache);

  Superblock sb;
  EXPECT_OK(bcache->Readblk(0, &sb));
  char data[kMinfsBlockSize];

  blk_t journal_block = static_cast<blk_t>(JournalStartBlock(sb));
  EXPECT_OK(bcache->Readblk(journal_block, data));

  // Check that the journal superblock is valid.
  fs::JournalInfo* journal_info = reinterpret_cast<fs::JournalInfo*>(data);
  EXPECT_EQ(journal_info->magic, fs::kJournalMagic);

  // Corrupt the journalInfo checksum
  journal_info->checksum = 0;
  EXPECT_OK(bcache->Writeblk(journal_block, data));

  ASSERT_NOT_OK(Fsck(std::move(bcache), FsckOptions{.repair = false}, &bcache));
}

}  // namespace
}  // namespace minfs
