// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <block-client/cpp/fake-device.h>
#include <fbl/auto_call.h>
#include <zxtest/zxtest.h>

#include "src/storage/minfs/file.h"
#include "src/storage/minfs/format.h"
#include "src/storage/minfs/fsck.h"
#include "src/storage/minfs/minfs_private.h"
#include "src/storage/minfs/test/unit/journal_integration_fixture.h"

namespace minfs {
namespace {

class JournalIntegrationTest : public JournalIntegrationFixture {
 private:
  // Creates an entry in the root of the filesystem and synchronizing writeback operations to
  // storage.
  void PerformOperation(Minfs* fs) override {
    fbl::RefPtr<VnodeMinfs> root;
    ASSERT_OK(fs->VnodeGet(&root, kMinfsRootIno));

    fbl::RefPtr<fs::Vnode> child;
    ASSERT_OK(root->Create("foo", 0, &child));
    ASSERT_OK(child->Close());
  }
};

// WARNING: The numbers here may change if the filesystem issues different write patterns.
// The important properties to preserve are:
// - Fsck (without journal replay) should fail.
// - Fsck (with journal replay) should succeed.
constexpr uint64_t kCreateEntryCutoff = 4 * JournalIntegrationTest::kDiskBlocksPerFsBlock;

TEST_F(JournalIntegrationTest, FsckWithRepairDoesReplayJournal) {
  auto bcache = CutOffDevice(write_count() - kCreateEntryCutoff);
  EXPECT_OK(Fsck(std::move(bcache), FsckOptions{.repair = true}, &bcache));

  // We should be able to re-run fsck with the same results, with or without repairing.
  EXPECT_OK(Fsck(std::move(bcache), FsckOptions{.repair = true}, &bcache));
  EXPECT_OK(Fsck(std::move(bcache), FsckOptions{.repair = false}, &bcache));
}

TEST_F(JournalIntegrationTest, FsckWithoutRepairDoesNotReplayJournal) {
  auto bcache = CutOffDevice(write_count() - kCreateEntryCutoff);
  EXPECT_NOT_OK(Fsck(std::move(bcache), FsckOptions{.repair = false}, &bcache));
}

TEST_F(JournalIntegrationTest, CreateWithoutRepairDoesNotReplayJournal) {
  auto bcache = CutOffDevice(write_count() - kCreateEntryCutoff);

  MountOptions options = {};
  options.repair_filesystem = false;
  options.use_journal = false;
  std::unique_ptr<Minfs> fs;
  EXPECT_OK(Minfs::Create(std::move(bcache), options, &fs));
  bcache = Minfs::Destroy(std::move(fs));
  EXPECT_NOT_OK(Fsck(std::move(bcache), FsckOptions(), &bcache));
}

TEST_F(JournalIntegrationTest, CreateWithRepairDoesReplayJournal) {
  auto bcache = CutOffDevice(write_count() - kCreateEntryCutoff);

  MountOptions options = {};
  std::unique_ptr<Minfs> fs;
  EXPECT_OK(Minfs::Create(std::move(bcache), options, &fs));
  bcache = Minfs::Destroy(std::move(fs));
  EXPECT_OK(Fsck(std::move(bcache), FsckOptions(), &bcache));
}

class JournalUnlinkTest : public JournalIntegrationFixture {
 private:
  // Creating but also removing an entry from the root of the filesystem, while a connection to the
  // unlinked vnode remains alive.
  void PerformOperation(Minfs* fs) override {
    fbl::RefPtr<VnodeMinfs> root;
    ASSERT_OK(fs->VnodeGet(&root, kMinfsRootIno));

    fbl::RefPtr<fs::Vnode> foo, bar, baz;
    ASSERT_OK(root->Create("foo", 0, &foo));
    ASSERT_OK(root->Create("bar", 0, &bar));
    ASSERT_OK(root->Create("baz", 0, &baz));
    ASSERT_OK(root->Unlink("foo", false));
    ASSERT_OK(root->Unlink("bar", false));
    ASSERT_OK(root->Unlink("baz", false));

    RecordWriteCount(fs);

    // This should succeed on the first pass when measuring, but will fail on the second pass when
    // the fake device starts to fail writes.
    foo->Close();
    bar->Close();
    baz->Close();
  }
};

// Cuts the "unlink" operation off. Unlink typically needs to update
// the parent inode, the parent directory, and the inode allocation bitmap.
// By cutting the operation in two (without replay), the consistency checker
// should be able to identify inconsistent link counts between the multiple
// data structures.
constexpr uint64_t kUnlinkCutoff = 3 * JournalUnlinkTest::kDiskBlocksPerFsBlock;

TEST_F(JournalUnlinkTest, FsckWithRepairDoesReplayJournal) {
  auto bcache = CutOffDevice(write_count() - kUnlinkCutoff);
  EXPECT_OK(Fsck(std::move(bcache), FsckOptions{.repair = true}, &bcache));

  // We should be able to re-run fsck with the same results, with or without repairing.
  EXPECT_OK(Fsck(std::move(bcache), FsckOptions{.repair = true}, &bcache));
  EXPECT_OK(Fsck(std::move(bcache), FsckOptions{.repair = false}, &bcache));
}

TEST_F(JournalUnlinkTest, FsckWithoutRepairDoesNotReplayJournal) {
  auto bcache = CutOffDevice(write_count() - kUnlinkCutoff);
  EXPECT_NOT_OK(Fsck(std::move(bcache), FsckOptions{.repair = false}, &bcache));
}

class JournalGrowFvmTest : public JournalIntegrationFixture {
 private:
  void PerformOperation(Minfs* fs) override {
    fbl::RefPtr<VnodeMinfs> root;
    ASSERT_OK(fs->VnodeGet(&root, kMinfsRootIno));
    fbl::RefPtr<fs::Vnode> foo, bar, baz;
    ASSERT_OK(root->Create("foo", 0, &foo));
    // Write to a file until we cause an FVM extension.
    std::vector<uint8_t> buf(TransactionLimits::kMaxWriteBytes);
    size_t done = 0;
    uint32_t slices = fs->Info().dat_slices;
    while (fs->Info().dat_slices == slices) {
      size_t written;
      ASSERT_OK(foo->Write(buf.data(), buf.size(), done, &written));
      ASSERT_EQ(written, buf.size());
      done += written;
    }
    ASSERT_OK(foo->Close());
  }
};

constexpr uint64_t kGrowFvmCutoff = 6 * JournalGrowFvmTest::kDiskBlocksPerFsBlock;

TEST_F(JournalGrowFvmTest, GrowingWithJournalReplaySucceeds) {
  auto bcache = CutOffDevice(write_count() - kGrowFvmCutoff);
  EXPECT_OK(Fsck(std::move(bcache), FsckOptions{.repair = true}, &bcache));
  std::unique_ptr<Minfs> fs;
  ASSERT_OK(Minfs::Create(std::move(bcache), MountOptions(), &fs));
  EXPECT_EQ(2, fs->Info().dat_slices);  // We expect the increased size.
}

TEST_F(JournalGrowFvmTest, GrowingWithNoReplaySucceeds) {
  // In this test, 1 fewer block means the replay will fail.
  auto bcache = CutOffDevice(write_count() - kGrowFvmCutoff - kDiskBlocksPerFsBlock);
  EXPECT_OK(Fsck(std::move(bcache), FsckOptions{.repair = true}, &bcache));
  std::unique_ptr<Minfs> fs;
  ASSERT_OK(Minfs::Create(std::move(bcache), MountOptions(), &fs));
  EXPECT_EQ(1, fs->Info().dat_slices);  // We expect the old, smaller size.
}

// It is not safe for data writes to go to freed blocks until the metadata that frees them has been
// committed because data writes do not wait. This test verifies this by pausing writes and then
// freeing blocks and making sure that block doesn't get reused. This test currently relies on the
// allocator behaving a certain way, i.e. it allocates the first free block that it can find.
TEST(JournalAllocationTest, BlocksAreReservedUntilMetadataIsCommitted) {
  static constexpr int kBlockCount = 1 << 15;
  auto device = std::make_unique<block_client::FakeBlockDevice>(kBlockCount, 512);
  block_client::FakeBlockDevice* device_ptr = device.get();
  std::unique_ptr<Bcache> bcache;
  ASSERT_OK(Bcache::Create(std::move(device), kBlockCount, &bcache));
  ASSERT_OK(Mkfs(bcache.get()));
  MountOptions options = {};
  std::unique_ptr<Minfs> fs;
  ASSERT_OK(Minfs::Create(std::move(bcache), options, &fs));

  // Create a file and make it allocate 1 block.
  fbl::RefPtr<VnodeMinfs> root;
  ASSERT_OK(fs->VnodeGet(&root, kMinfsRootIno));
  fbl::RefPtr<fs::Vnode> foo;
  ASSERT_OK(root->Create("foo", 0, &foo));
  auto close = fbl::MakeAutoCall([foo]() { ASSERT_OK(foo->Close()); });
  std::vector<uint8_t> buf(10, 0xaf);
  size_t written;
  ASSERT_OK(foo->Write(buf.data(), buf.size(), 0, &written));
  ASSERT_EQ(written, buf.size());

  // Make a note of which block was allocated.
  auto foo_file = fbl::RefPtr<File>::Downcast(foo);
  blk_t block = foo_file->GetInode()->dnum[0];
  EXPECT_NE(0, block);

  // Pause writes now.
  device_ptr->Pause();

  // Truncate the file which should cause the block to be released.
  ASSERT_OK(foo->Truncate(0));

  // Write to the file again and make sure it gets written to a different block.
  ASSERT_OK(foo->Write(buf.data(), buf.size(), 0, &written));
  ASSERT_EQ(written, buf.size());

  // The block that was allocated should be different.
  EXPECT_NE(block, foo_file->GetInode()->dnum[0]);

  // Resume so that fs can be destroyed.
  device_ptr->Resume();
}

}  // namespace
}  // namespace minfs
