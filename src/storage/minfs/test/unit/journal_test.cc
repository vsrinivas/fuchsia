// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <block-client/cpp/fake-device.h>
#include <fbl/auto_call.h>
#include <gtest/gtest.h>

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
    ASSERT_EQ(fs->VnodeGet(&root, kMinfsRootIno), ZX_OK);

    fbl::RefPtr<fs::Vnode> child;
    ASSERT_EQ(root->Create("foo", 0, &child), ZX_OK);
    ASSERT_EQ(child->Close(), ZX_OK);
  }
};

// WARNING: The numbers here may change if the filesystem issues different write patterns.  Sadly,
// if write patterns do change, careful debugging needs to be done to find the new correct values.
//
// The important properties to preserve are:
// - Fsck (without journal replay) should fail.
// - Fsck (with journal replay) should succeed.
constexpr uint64_t kCreateEntryCutoff = 4 * JournalIntegrationTest::kDiskBlocksPerFsBlock;

TEST_F(JournalIntegrationTest, FsckWithRepairDoesReplayJournal) {
  auto bcache = CutOffDevice(write_count() - kCreateEntryCutoff);
  EXPECT_EQ(Fsck(std::move(bcache), FsckOptions{.repair = true}, &bcache), ZX_OK);

  // We should be able to re-run fsck with the same results, with or without repairing.
  EXPECT_EQ(Fsck(std::move(bcache), FsckOptions{.repair = true}, &bcache), ZX_OK);
  EXPECT_EQ(Fsck(std::move(bcache), FsckOptions{.repair = false}, &bcache), ZX_OK);
}

TEST_F(JournalIntegrationTest, FsckWithReadOnlyDoesNotReplayJournal) {
  auto bcache = CutOffDevice(write_count() - kCreateEntryCutoff);
  EXPECT_NE(Fsck(std::move(bcache), FsckOptions{.repair = false, .read_only = true}, &bcache),
            ZX_OK);
}

TEST_F(JournalIntegrationTest, CreateWithRepairDoesReplayJournal) {
  auto bcache = CutOffDevice(write_count() - kCreateEntryCutoff);

  MountOptions options = {};
  std::unique_ptr<Minfs> fs;
  EXPECT_EQ(Minfs::Create(std::move(bcache), options, &fs), ZX_OK);
  bcache = Minfs::Destroy(std::move(fs));
  EXPECT_EQ(Fsck(std::move(bcache), FsckOptions(), &bcache), ZX_OK);
}

class JournalUnlinkTest : public JournalIntegrationFixture {
 private:
  // Creating but also removing an entry from the root of the filesystem, while a connection to the
  // unlinked vnode remains alive.
  void PerformOperation(Minfs* fs) override {
    fbl::RefPtr<VnodeMinfs> root;
    ASSERT_EQ(fs->VnodeGet(&root, kMinfsRootIno), ZX_OK);

    fbl::RefPtr<fs::Vnode> foo, bar, baz;
    ASSERT_EQ(root->Create("foo", 0, &foo), ZX_OK);
    ASSERT_EQ(root->Create("bar", 0, &bar), ZX_OK);
    ASSERT_EQ(root->Create("baz", 0, &baz), ZX_OK);
    ASSERT_EQ(root->Unlink("foo", false), ZX_OK);
    ASSERT_EQ(root->Unlink("bar", false), ZX_OK);
    ASSERT_EQ(root->Unlink("baz", false), ZX_OK);

    RecordWriteCount(fs);

    // This should succeed on the first pass when measuring, but will fail on the second pass when
    // the fake device starts to fail writes.
    foo->Close();
    bar->Close();
    baz->Close();
  }
};

// Cuts the "unlink" operation off. Unlink typically needs to update the parent inode, the parent
// directory, and the inode allocation bitmap.  By cutting the operation in two (without replay),
// the consistency checker should be able to identify inconsistent link counts between the multiple
// data structures.
//
// See note at beginning regarding tuning these numbers.
constexpr uint64_t kUnlinkCutoff = 3 * JournalUnlinkTest::kDiskBlocksPerFsBlock;

TEST_F(JournalUnlinkTest, FsckWithRepairDoesReplayJournal) {
  auto bcache = CutOffDevice(write_count() - kUnlinkCutoff);
  EXPECT_EQ(Fsck(std::move(bcache), FsckOptions{.repair = true}, &bcache), ZX_OK);

  // We should be able to re-run fsck with the same results, with or without repairing.
  EXPECT_EQ(Fsck(std::move(bcache), FsckOptions{.repair = true}, &bcache), ZX_OK);
  EXPECT_EQ(Fsck(std::move(bcache), FsckOptions{.repair = false}, &bcache), ZX_OK);
}

TEST_F(JournalUnlinkTest, ReadOnlyFsckDoesNotReplayJournal) {
  auto bcache = CutOffDevice(write_count() - kUnlinkCutoff);
  EXPECT_NE(Fsck(std::move(bcache), FsckOptions{.repair = false, .read_only = true}, &bcache),
            ZX_OK);
}

class JournalGrowFvmTest : public JournalIntegrationFixture {
 private:
  void PerformOperation(Minfs* fs) override {
    fbl::RefPtr<VnodeMinfs> root;
    ASSERT_EQ(fs->VnodeGet(&root, kMinfsRootIno), ZX_OK);
    fbl::RefPtr<fs::Vnode> foo, bar, baz;
    ASSERT_EQ(root->Create("foo", 0, &foo), ZX_OK);
    // Write to a file until we cause an FVM extension.
    std::vector<uint8_t> buf(TransactionLimits::kMaxWriteBytes);
    size_t done = 0;
    uint32_t slices = fs->Info().dat_slices;
    while (fs->Info().dat_slices == slices) {
      size_t written;
      ASSERT_EQ(foo->Write(buf.data(), buf.size(), done, &written), ZX_OK);
      ASSERT_EQ(written, buf.size());
      done += written;
    }
    ASSERT_EQ(foo->Close(), ZX_OK);
    // The infrastructure relies on the number of blocks written to block device
    // to function properly. Sync here ensures that what was written in this
    // function gets persisted to underlying block device.
    sync_completion_t completion;
    fs->Sync([&completion](zx_status_t status) { sync_completion_signal(&completion); });
    ASSERT_EQ(sync_completion_wait(&completion, zx::duration::infinite().get()), ZX_OK);
  }
};

// See note at beginning regarding tuning these numbers.
constexpr uint64_t kGrowFvmCutoff = 32 * JournalGrowFvmTest::kDiskBlocksPerFsBlock;

TEST_F(JournalGrowFvmTest, GrowingWithJournalReplaySucceeds) {
  auto bcache = CutOffDevice(write_count());
  EXPECT_EQ(Fsck(std::move(bcache), FsckOptions{.repair = true}, &bcache), ZX_OK);
  std::unique_ptr<Minfs> fs;
  ASSERT_EQ(Minfs::Create(std::move(bcache), MountOptions(), &fs), ZX_OK);
  EXPECT_EQ(fs->Info().dat_slices, 2u);  // We expect the increased size.
}

TEST_F(JournalGrowFvmTest, GrowingWithNoReplaySucceeds) {
  // In this test, 1 fewer block means the replay will fail.
  auto bcache = CutOffDevice(write_count() - kGrowFvmCutoff - kDiskBlocksPerFsBlock);
  EXPECT_EQ(Fsck(std::move(bcache), FsckOptions{.repair = true}, &bcache), ZX_OK);
  std::unique_ptr<Minfs> fs;
  ASSERT_EQ(Minfs::Create(std::move(bcache), MountOptions(), &fs), ZX_OK);
  EXPECT_EQ(fs->Info().dat_slices, 1u);  // We expect the old, smaller size.
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
  ASSERT_EQ(Bcache::Create(std::move(device), kBlockCount, &bcache), ZX_OK);
  ASSERT_EQ(Mkfs(bcache.get()), ZX_OK);
  MountOptions options = {};
  std::unique_ptr<Minfs> fs;
  ASSERT_EQ(Minfs::Create(std::move(bcache), options, &fs), ZX_OK);

  // Create a file and make it allocate 1 block.
  fbl::RefPtr<VnodeMinfs> root;
  ASSERT_EQ(fs->VnodeGet(&root, kMinfsRootIno), ZX_OK);
  fbl::RefPtr<fs::Vnode> foo;
  ASSERT_EQ(root->Create("foo", 0, &foo), ZX_OK);
  auto close = fbl::MakeAutoCall([foo]() { ASSERT_EQ(foo->Close(), ZX_OK); });
  std::vector<uint8_t> buf(10, 0xaf);
  size_t written;
  ASSERT_EQ(foo->Write(buf.data(), buf.size(), 0, &written), ZX_OK);
  sync_completion_t completion;
  foo->Sync([&completion](zx_status_t status) { sync_completion_signal(&completion); });
  ASSERT_EQ(sync_completion_wait(&completion, zx::duration::infinite().get()), ZX_OK);
  ASSERT_EQ(written, buf.size());

  // Make a note of which block was allocated.
  auto foo_file = fbl::RefPtr<File>::Downcast(foo);
  blk_t block = foo_file->GetInode()->dnum[0];
  EXPECT_NE(block, 0u);

  // Pause writes now.
  device_ptr->Pause();

  // Truncate the file which should cause the block to be released.
  ASSERT_EQ(foo->Truncate(0), ZX_OK);

  // Write to the file again and make sure it gets written to a different block.
  ASSERT_EQ(foo->Write(buf.data(), buf.size(), 0, &written), ZX_OK);
  ASSERT_EQ(written, buf.size());

  // The block that was allocated should be different.
  EXPECT_NE(block, foo_file->GetInode()->dnum[0]);

  // Resume so that fs can be destroyed.
  device_ptr->Resume();
}

}  // namespace
}  // namespace minfs
