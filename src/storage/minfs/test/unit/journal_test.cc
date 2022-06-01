// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/defer.h>

#include <gtest/gtest.h>

#include "src/lib/storage/block_client/cpp/fake_block_device.h"
#include "src/storage/minfs/file.h"
#include "src/storage/minfs/format.h"
#include "src/storage/minfs/fsck.h"
#include "src/storage/minfs/runner.h"
#include "src/storage/minfs/test/unit/journal_integration_fixture.h"

namespace minfs {
namespace {

class JournalIntegrationTest : public JournalIntegrationFixture {
 private:
  // Creates an entry in the root of the filesystem and synchronizing writeback operations to
  // storage.
  void PerformOperation(Minfs& fs) override {
    auto root_or = fs.VnodeGet(kMinfsRootIno);
    ASSERT_TRUE(root_or.is_ok());

    fbl::RefPtr<fs::Vnode> child;
    ASSERT_EQ(root_or->Create("foo", 0, &child), ZX_OK);
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
  zx::status<std::unique_ptr<Bcache>> bcache_or =
      zx::ok(CutOffDevice(write_count() - kCreateEntryCutoff));

  bcache_or = Fsck(std::move(bcache_or.value()), FsckOptions{.repair = true});
  EXPECT_TRUE(bcache_or.is_ok());

  // We should be able to re-run fsck with the same results, with or without repairing.
  bcache_or = Fsck(std::move(bcache_or.value()), FsckOptions{.repair = true});
  EXPECT_TRUE(bcache_or.is_ok());

  bcache_or = Fsck(std::move(bcache_or.value()), FsckOptions{.repair = false});
  EXPECT_TRUE(bcache_or.is_ok());
}

TEST_F(JournalIntegrationTest, FsckWithReadOnlyDoesNotReplayJournal) {
  zx::status<std::unique_ptr<Bcache>> bcache_or =
      zx::ok(CutOffDevice(write_count() - kCreateEntryCutoff));
  EXPECT_TRUE(Fsck(std::move(bcache_or.value()), FsckOptions{.repair = false, .read_only = true})
                  .is_error());
}

TEST_F(JournalIntegrationTest, CreateWithRepairDoesReplayJournal) {
  zx::status<std::unique_ptr<Bcache>> bcache_or =
      zx::ok(CutOffDevice(write_count() - kCreateEntryCutoff));

  MountOptions options = {};
  auto fs_or = Runner::Create(dispatcher(), std::move(bcache_or.value()), options);
  EXPECT_TRUE(fs_or.is_ok());
  bcache_or = zx::ok(Runner::Destroy(std::move(fs_or.value())));
  EXPECT_TRUE(Fsck(std::move(bcache_or.value()), FsckOptions()).is_ok());
}

class JournalUnlinkTest : public JournalIntegrationFixture {
 private:
  // Creating but also removing an entry from the root of the filesystem, while a connection to the
  // unlinked vnode remains alive.
  void PerformOperation(Minfs& fs) override {
    auto root_or = fs.VnodeGet(kMinfsRootIno);
    ASSERT_TRUE(root_or.is_ok());

    fbl::RefPtr<fs::Vnode> foo, bar, baz;
    ASSERT_EQ(root_or->Create("foo", 0, &foo), ZX_OK);
    ASSERT_EQ(root_or->Create("bar", 0, &bar), ZX_OK);
    ASSERT_EQ(root_or->Create("baz", 0, &baz), ZX_OK);
    ASSERT_EQ(root_or->Unlink("foo", false), ZX_OK);
    ASSERT_EQ(root_or->Unlink("bar", false), ZX_OK);
    ASSERT_EQ(root_or->Unlink("baz", false), ZX_OK);

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
  zx::status<std::unique_ptr<Bcache>> bcache_or =
      zx::ok(CutOffDevice(write_count() - kUnlinkCutoff));
  bcache_or = Fsck(std::move(bcache_or.value()), FsckOptions{.repair = true});
  EXPECT_TRUE(bcache_or.is_ok());

  // We should be able to re-run fsck with the same results, with or without repairing.
  bcache_or = Fsck(std::move(bcache_or.value()), FsckOptions{.repair = true});
  EXPECT_TRUE(bcache_or.is_ok());
  bcache_or = Fsck(std::move(bcache_or.value()), FsckOptions{.repair = false});
  EXPECT_TRUE(bcache_or.is_ok());
}

TEST_F(JournalUnlinkTest, ReadOnlyFsckDoesNotReplayJournal) {
  zx::status<std::unique_ptr<Bcache>> bcache_or =
      zx::ok(CutOffDevice(write_count() - kUnlinkCutoff));
  bcache_or = Fsck(std::move(bcache_or.value()), FsckOptions{.repair = false, .read_only = true});
  EXPECT_TRUE(bcache_or.is_error());
}

class JournalGrowFvmTest : public JournalIntegrationFixture {
 private:
  void PerformOperation(Minfs& fs) override {
    auto root_or = fs.VnodeGet(kMinfsRootIno);
    ASSERT_TRUE(root_or.is_ok());
    fbl::RefPtr<fs::Vnode> foo, bar, baz;
    ASSERT_EQ(root_or->Create("foo", 0, &foo), ZX_OK);
    // Write to a file until we cause an FVM extension.
    std::vector<uint8_t> buf(TransactionLimits::kMaxWriteBytes);
    size_t done = 0;
    uint32_t slices = fs.Info().dat_slices;
    while (fs.Info().dat_slices == slices) {
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
    fs.Sync([&completion](zx_status_t status) { sync_completion_signal(&completion); });
    ASSERT_EQ(sync_completion_wait(&completion, zx::duration::infinite().get()), ZX_OK);
  }
};

// See note at beginning regarding tuning these numbers.
constexpr uint64_t kGrowFvmCutoff = 32 * JournalGrowFvmTest::kDiskBlocksPerFsBlock;

TEST_F(JournalGrowFvmTest, GrowingWithJournalReplaySucceeds) {
  zx::status<std::unique_ptr<Bcache>> bcache_or = zx::ok(CutOffDevice(write_count()));
  bcache_or = Fsck(std::move(bcache_or.value()), FsckOptions{.repair = true});
  EXPECT_TRUE(bcache_or.is_ok());
  auto fs_or = Runner::Create(dispatcher(), std::move(bcache_or.value()), MountOptions());
  ASSERT_TRUE(fs_or.is_ok());
  EXPECT_EQ(fs_or->minfs().Info().dat_slices, 2u);  // We expect the increased size.
}

TEST_F(JournalGrowFvmTest, GrowingWithNoReplaySucceeds) {
  // In this test, 1 fewer block means the replay will fail.
  zx::status<std::unique_ptr<Bcache>> bcache_or =
      zx::ok(CutOffDevice(write_count() - kGrowFvmCutoff - kDiskBlocksPerFsBlock));

  bcache_or = Fsck(std::move(bcache_or.value()), FsckOptions{.repair = true});
  EXPECT_TRUE(bcache_or.is_ok());
  auto fs_or = Runner::Create(dispatcher(), std::move(bcache_or.value()), MountOptions());
  ASSERT_TRUE(fs_or.is_ok());
  EXPECT_EQ(fs_or->minfs().Info().dat_slices, 1u);  // We expect the old, smaller size.
}

// It is not safe for data writes to go to freed blocks until the metadata that frees them has been
// committed because data writes do not wait. This test verifies this by pausing writes and then
// freeing blocks and making sure that block doesn't get reused. This test currently relies on the
// allocator behaving a certain way, i.e. it allocates the first free block that it can find.
TEST_F(JournalIntegrationTest, BlocksAreReservedUntilMetadataIsCommitted) {
  static constexpr int kBlockCount = 1 << 15;
  auto device = std::make_unique<block_client::FakeBlockDevice>(kBlockCount, 512);
  block_client::FakeBlockDevice* device_ptr = device.get();
  auto bcache_or = Bcache::Create(std::move(device), kBlockCount);
  ASSERT_TRUE(bcache_or.is_ok());
  ASSERT_TRUE(Mkfs(bcache_or.value().get()).is_ok());
  MountOptions options = {};
  auto fs_or = Runner::Create(dispatcher(), std::move(bcache_or.value()), options);
  ASSERT_TRUE(fs_or.is_ok());

  // Create a file and make it allocate 1 block.
  auto root_or = fs_or->minfs().VnodeGet(kMinfsRootIno);
  ASSERT_TRUE(root_or.is_ok());
  fbl::RefPtr<fs::Vnode> foo;
  ASSERT_EQ(root_or->Create("foo", 0, &foo), ZX_OK);
  auto close = fit::defer([foo]() { ASSERT_EQ(foo->Close(), ZX_OK); });
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
