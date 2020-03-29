// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <block-client/cpp/fake-device.h>
#include <lib/fit/function.h>
#include <lib/sync/completion.h>
#include <minfs/format.h>
#include <minfs/fsck.h>
#include <zxtest/zxtest.h>

#include "minfs-private.h"

namespace minfs {
namespace {

using block_client::FakeBlockDevice;

constexpr uint64_t kBlockCount = 1 << 15;
constexpr uint32_t kBlockSize = 512;
constexpr uint32_t kDiskBlocksPerFsBlock = kMinfsBlockSize / kBlockSize;

// Helper for conversion from "Bcache" to "FakeBlockDevice".
void TakeDeviceFromBcache(std::unique_ptr<Bcache> bcache,
                          std::unique_ptr<block_client::FakeBlockDevice>* out) {
  std::unique_ptr<block_client::BlockDevice> block_device =
    Bcache::Destroy(std::move(bcache));
  out->reset(reinterpret_cast<block_client::FakeBlockDevice*>(block_device.release()));
}

// Helper for conversion from "Minfs" to "FakeBlockDevice".
void TakeDeviceFromMinfs(std::unique_ptr<Minfs> minfs,
                         std::unique_ptr<block_client::FakeBlockDevice>* out) {
  std::unique_ptr<Bcache> bcache = Minfs::Destroy(std::move(minfs));
  TakeDeviceFromBcache(std::move(bcache), out);
}

using OperationCallback = void (Minfs*);

// Collects the number of write operations necessary to perform an operation.
//
// Reformats the provided |in_out_device|, which acts as both an input and output parameter.
void CountWritesToPerformOperation(OperationCallback operation,
                                   std::unique_ptr<FakeBlockDevice>* in_out_device,
                                   uint64_t* write_count) {
  auto device = std::move(*in_out_device);
  std::unique_ptr<Bcache> bcache;
  ASSERT_OK(Bcache::Create(std::move(device), kBlockCount, &bcache));

  ASSERT_OK(Mkfs(bcache.get()));
  // After formatting the device, count the number of blocks issued to the underlying device.
  TakeDeviceFromBcache(std::move(bcache), &device);
  device->ResetBlockCounts();
  ASSERT_OK(Bcache::Create(std::move(device), kBlockCount, &bcache));
  MountOptions options = {};
  std::unique_ptr<Minfs> fs;
  ASSERT_OK(Minfs::Create(std::move(bcache), options, &fs));

  // Perform the caller-requested operation.
  operation(fs.get());

  TakeDeviceFromMinfs(std::move(fs), &device);
  *write_count = device->GetWriteBlockCount();
  *in_out_device = std::move(device);
}

// Performs a user-requested operation with a "write limit".
//
// See "CountWritesToPerformOperation" for a reasonable |write_count| value to set.
void PerformOperationWithTransactionLimit(OperationCallback operation,
                                          uint64_t write_count,
                                          std::unique_ptr<FakeBlockDevice>* in_out_device) {
  auto device = std::move(*in_out_device);
  std::unique_ptr<Bcache> bcache;
  ASSERT_OK(Bcache::Create(std::move(device), kBlockCount, &bcache));

  ASSERT_OK(Mkfs(bcache.get()));
  // After formatting the device, create a transaction limit on the underlying device.
  TakeDeviceFromBcache(std::move(bcache), &device);
  device->ResetBlockCounts();
  device->SetWriteBlockLimit(write_count);
  ASSERT_OK(Bcache::Create(std::move(device), kBlockCount, &bcache));
  MountOptions options = {};
  std::unique_ptr<Minfs> fs;
  ASSERT_OK(Minfs::Create(std::move(bcache), options, &fs));

  // Perform the caller-requested operation.
  operation(fs.get());

  TakeDeviceFromMinfs(std::move(fs), &device);
  device->ResetWriteBlockLimit();
  *in_out_device = std::move(device);
}

// A fixture which creates a filesystem image that "needs journal replay to be correct".
template <OperationCallback Op>
class JournalIntegrationFixture : public zxtest::Test {
 public:

  void SetUp() override {
    auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
    ASSERT_NO_FAILURES(CountWritesToPerformOperation(Op, &device, &write_count_));
  }

  // Returns the total number of disk block writes to complete |Op|.
  uint64_t TotalWrites() const {
    return write_count_;
  }

  // Returns a device which attempts to perform |Op|, but has a limit
  // of |allowed_blocks_blocks| writable disk blocks.
  std::unique_ptr<Bcache> CutOffDevice(uint64_t allowed_blocks) {
    auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
    // Attempt to "cut-off" the operation partway by reducing the number of writes.
    //
    // WARNING: This number may change if the filesystem issues different write patterns.
    // The important properties to preserve are:
    // - Fsck (without journal replay) should fail.
    // - Fsck (with journal replay) should succeed.
    PerformOperationWithTransactionLimit(Op, allowed_blocks, &device);
    std::unique_ptr<Bcache> bcache;
    EXPECT_OK(Bcache::Create(std::move(device), kBlockCount, &bcache));
    return bcache;
  }

 private:
  // Disk block writes to perform the operation normally.
  uint64_t write_count_ = 0;
};

// A callback for creating an entry in the root of the filesystem and synchronizing
// writeback operations to storage.
void CreateEntryOperation(Minfs* fs) {
  fbl::RefPtr<VnodeMinfs> root;
  ASSERT_OK(fs->VnodeGet(&root, kMinfsRootIno));

  fbl::RefPtr<fs::Vnode> child;
  ASSERT_OK(root->Create(&child, "foo", 0));
  ASSERT_OK(child->Close());

  sync_completion_t completion;
  fs->Sync([&completion](zx_status_t status) {
    sync_completion_signal(&completion);
  });
  ASSERT_OK(sync_completion_wait(&completion, zx::duration::infinite().get()));
  // Ignore the result of |Sync|, as the operation may or may not have failed.
}

using JournalIntegrationTest = JournalIntegrationFixture<CreateEntryOperation>;
constexpr uint64_t kCreateEntryCutoff = 10 * kDiskBlocksPerFsBlock;

TEST_F(JournalIntegrationTest, FsckWithRepairDoesReplayJournal) {
  auto bcache = CutOffDevice(TotalWrites() - kCreateEntryCutoff);
  EXPECT_OK(Fsck(std::move(bcache), Repair::kEnabled, &bcache));

  // We should be able to re-run fsck with the same results, with or without repairing.
  EXPECT_OK(Fsck(std::move(bcache), Repair::kEnabled, &bcache));
  EXPECT_OK(Fsck(std::move(bcache), Repair::kDisabled, &bcache));
}

TEST_F(JournalIntegrationTest, FsckWithoutRepairDoesNotReplayJournal) {
  auto bcache = CutOffDevice(TotalWrites() - kCreateEntryCutoff);
  EXPECT_NOT_OK(Fsck(std::move(bcache), Repair::kDisabled, &bcache));
}

TEST_F(JournalIntegrationTest, CreateWithoutRepairDoesNotReplayJournal) {
  auto bcache = CutOffDevice(TotalWrites() - kCreateEntryCutoff);

  MountOptions options = {};
  options.repair_filesystem = false;
  options.use_journal = false;
  std::unique_ptr<Minfs> fs;
  EXPECT_OK(Minfs::Create(std::move(bcache), options, &fs));
  bcache = Minfs::Destroy(std::move(fs));
  EXPECT_NOT_OK(Fsck(std::move(bcache), Repair::kDisabled, &bcache));
}

TEST_F(JournalIntegrationTest, CreateWithRepairDoesReplayJournal) {
  auto bcache = CutOffDevice(TotalWrites() - kCreateEntryCutoff);

  MountOptions options = {};
  std::unique_ptr<Minfs> fs;
  EXPECT_OK(Minfs::Create(std::move(bcache), options, &fs));
  bcache = Minfs::Destroy(std::move(fs));
  EXPECT_OK(Fsck(std::move(bcache), Repair::kDisabled, &bcache));
}

// A callback for creating but also removing an entry from the root of the filesystem,
// while a connection to the unlinked vnode remains alive.
void UnlinkEntryOperation(Minfs* fs) {
  fbl::RefPtr<VnodeMinfs> root;
  ASSERT_OK(fs->VnodeGet(&root, kMinfsRootIno));

  fbl::RefPtr<fs::Vnode> foo, bar, baz;
  ASSERT_OK(root->Create(&foo, "foo", 0));
  ASSERT_OK(root->Create(&bar, "bar", 0));
  ASSERT_OK(root->Create(&baz, "baz", 0));
  ASSERT_OK(root->Unlink("foo", false));
  ASSERT_OK(root->Unlink("bar", false));
  ASSERT_OK(root->Unlink("baz", false));

  sync_completion_t completion;
  fs->Sync([&completion](zx_status_t status) {
    sync_completion_signal(&completion);
  });
  ASSERT_OK(sync_completion_wait(&completion, zx::duration::infinite().get()));
  // Ignore the result of |Sync|, as the operation may or may not have failed.
  ASSERT_OK(foo->Close());
  ASSERT_OK(bar->Close());
  ASSERT_OK(baz->Close());
}

using JournalUnlinkTest = JournalIntegrationFixture<UnlinkEntryOperation>;

// Cuts the "unlink" operation off. Unlink typically needs to update
// the parent inode, the parent directory, and the inode allocation bitmap.
// By cutting the operation in two (without replay), the consistency checker
// should be able to identify inconsistent link counts between the multiple
// data structures.
constexpr uint64_t kUnlinkCutoff = 9 * kDiskBlocksPerFsBlock;

TEST_F(JournalUnlinkTest, FsckWithRepairDoesReplayJournal) {
  auto bcache = CutOffDevice(TotalWrites() - kUnlinkCutoff);
  EXPECT_OK(Fsck(std::move(bcache), Repair::kEnabled, &bcache));

  // We should be able to re-run fsck with the same results, with or without repairing.
  EXPECT_OK(Fsck(std::move(bcache), Repair::kEnabled, &bcache));
  EXPECT_OK(Fsck(std::move(bcache), Repair::kDisabled, &bcache));
}

TEST_F(JournalUnlinkTest, FsckWithoutRepairDoesNotReplayJournal) {
  auto bcache = CutOffDevice(TotalWrites() - kUnlinkCutoff);
  EXPECT_NOT_OK(Fsck(std::move(bcache), Repair::kDisabled, &bcache));
}

}  // namespace
}  // namespace minfs
