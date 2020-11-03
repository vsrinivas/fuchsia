// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/minfs_inspector.h"

#include <zircon/device/block.h>

#include <iostream>

#include <block-client/cpp/fake-device.h>
#include <disk_inspector/inspector_transaction_handler.h>
#include <disk_inspector/vmo_buffer_factory.h>
#include <fs/journal/format.h>
#include <zxtest/zxtest.h>

#include "src/storage/minfs/format.h"
#include "src/storage/minfs/minfs_private.h"

namespace minfs {
namespace {

using block_client::FakeBlockDevice;

constexpr uint64_t kBlockCount = 1 << 15;
constexpr uint32_t kBlockSize = 512;

void CreateMinfsInspector(std::unique_ptr<block_client::BlockDevice> device,
                          std::unique_ptr<MinfsInspector>* out) {
  std::unique_ptr<disk_inspector::InspectorTransactionHandler> inspector_handler;
  ASSERT_OK(disk_inspector::InspectorTransactionHandler::Create(std::move(device), kMinfsBlockSize,
                                                                &inspector_handler));
  auto buffer_factory =
      std::make_unique<disk_inspector::VmoBufferFactory>(inspector_handler.get(), kMinfsBlockSize);
  auto result = MinfsInspector::Create(std::move(inspector_handler), std::move(buffer_factory));
  ASSERT_TRUE(result.is_ok());
  *out = result.take_value();
}

// Initialize a MinfsInspector from a created fake block device formatted
// into a fresh minfs partition and journal entries.
void SetupMinfsInspector(std::unique_ptr<MinfsInspector>* inspector) {
  auto temp = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);

  // Format the device.
  std::unique_ptr<Bcache> bcache;
  ASSERT_OK(Bcache::Create(std::move(temp), kBlockCount, &bcache));
  ASSERT_OK(Mkfs(bcache.get()));

  // Write journal info to the device by creating a minfs and waiting for it
  // to finish.
  std::unique_ptr<Minfs> fs;
  MountOptions options = {};
  ASSERT_OK(minfs::Minfs::Create(std::move(bcache), options, &fs));
  sync_completion_t completion;
  fs->Sync([&completion](zx_status_t status) { sync_completion_signal(&completion); });
  ASSERT_OK(sync_completion_wait(&completion, zx::duration::infinite().get()));

  // We only care about the disk format written into the fake block device,
  // so we destroy the minfs/bcache used to format it.
  bcache = Minfs::Destroy(std::move(fs));
  CreateMinfsInspector(Bcache::Destroy(std::move(bcache)), inspector);
}

// Initialize a MinfsInspector from an zero-ed out block device. This simulates
// corruption to various metadata. Allows copying |count| bytes of |data| to
// the start of the fake block device.
void BadSetupMinfsInspector(std::unique_ptr<MinfsInspector>* inspector, void* data,
                            uint64_t count) {
  auto temp = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
  if (count > 0) {
    zx::vmo buffer;
    ASSERT_OK(zx::vmo::create(count, 0, &buffer));
    ASSERT_OK(buffer.write(data, 0, count));

    storage::OwnedVmoid vmoid;
    ASSERT_OK(temp->BlockAttachVmo(buffer, &vmoid.GetReference(temp.get())));

    std::vector<block_fifo_request_t> reqs = {{
        .opcode = BLOCKIO_WRITE,
        .reqid = 0x0,
        .group = 0,
        .vmoid = vmoid.get(),
        .length = static_cast<uint32_t>(count / kBlockSize),
        .vmo_offset = 0,
        .dev_offset = 0,
    }};
    ASSERT_OK(temp->FifoTransaction(reqs.data(), 1));
  }
  CreateMinfsInspector(std::move(temp), inspector);
}

TEST(MinfsInspector, CreateWithoutError) {
  std::unique_ptr<MinfsInspector> inspector;
  SetupMinfsInspector(&inspector);
}

TEST(MinfsInspector, CreateWithoutErrorOnBadSuperblock) {
  ASSERT_NO_DEATH(
      []() {
        std::unique_ptr<MinfsInspector> inspector;
        BadSetupMinfsInspector(&inspector, nullptr, 0);
      },
      "Could not initialize minfs inspector with bad superblock.");
}

TEST(MinfsInspector, InspectSuperblock) {
  std::unique_ptr<MinfsInspector> inspector;
  SetupMinfsInspector(&inspector);

  Superblock sb = inspector->InspectSuperblock();

  EXPECT_EQ(sb.magic0, kMinfsMagic0);
  EXPECT_EQ(sb.magic1, kMinfsMagic1);
  EXPECT_EQ(sb.format_version, kMinfsCurrentFormatVersion);
  EXPECT_EQ(sb.flags, kMinfsFlagClean);
  EXPECT_EQ(sb.block_size, kMinfsBlockSize);
  EXPECT_EQ(sb.inode_size, kMinfsInodeSize);
  EXPECT_EQ(sb.alloc_block_count, 2);
  EXPECT_EQ(sb.alloc_block_count, 2);
}

TEST(MinfsInspector, GetInodeCount) {
  std::unique_ptr<MinfsInspector> inspector;
  SetupMinfsInspector(&inspector);

  Superblock sb = inspector->InspectSuperblock();
  EXPECT_EQ(inspector->GetInodeCount(), sb.inode_count);
}

TEST(MinfsInspector, InspectInode) {
  std::unique_ptr<MinfsInspector> inspector;
  SetupMinfsInspector(&inspector);

  Superblock sb = inspector->InspectSuperblock();
  // The fresh minfs device should have 2 allocated inodes, empty inode 0 and
  // allocated inode 1.
  ASSERT_EQ(sb.alloc_inode_count, 2);

  auto result = inspector->InspectInodeRange(0, 3);
  ASSERT_TRUE(result.is_ok());
  std::vector<Inode> inodes = result.take_value();
  // 0th inode is uninitialized.

  Inode inode = inodes[0];
  EXPECT_EQ(inode.magic, 0);
  EXPECT_EQ(inode.size, 0);
  EXPECT_EQ(inode.block_count, 0);
  EXPECT_EQ(inode.link_count, 0);

  // 1st inode is initialized and is the root directory.
  inode = inodes[1];
  EXPECT_EQ(inode.magic, kMinfsMagicDir);
  EXPECT_EQ(inode.size, kMinfsBlockSize);
  EXPECT_EQ(inode.block_count, 1);
  EXPECT_EQ(inode.link_count, 2);

  // 2nd inode is uninitialized.
  inode = inodes[2];
  EXPECT_EQ(inode.magic, 0);
  EXPECT_EQ(inode.size, 0);
  EXPECT_EQ(inode.block_count, 0);
  EXPECT_EQ(inode.link_count, 0);
}

TEST(MinfsInspector, CheckInodeAllocated) {
  std::unique_ptr<MinfsInspector> inspector;
  SetupMinfsInspector(&inspector);

  Superblock sb = inspector->InspectSuperblock();
  ASSERT_TRUE(sb.alloc_inode_count < sb.inode_count);

  uint32_t max_samples = 10;
  uint32_t num_inodes_to_sample = (sb.inode_count < max_samples) ? sb.inode_count : max_samples;

  auto result = inspector->InspectInodeAllocatedInRange(0, num_inodes_to_sample);
  ASSERT_TRUE(result.is_ok());

  std::vector<uint64_t> allocated_indices = result.take_value();

  ASSERT_EQ(allocated_indices.size(), sb.alloc_inode_count);
  for (uint32_t i = 0; i < sb.alloc_inode_count; ++i) {
    EXPECT_EQ(allocated_indices[i], i);
  }
}

TEST(MinfsInspector, InspectJournalSuperblock) {
  std::unique_ptr<MinfsInspector> inspector;
  SetupMinfsInspector(&inspector);

  auto result = inspector->InspectJournalSuperblock();
  ASSERT_TRUE(result.is_ok());
  fs::JournalInfo journal_info = result.take_value();

  EXPECT_EQ(journal_info.magic, fs::kJournalMagic);
  EXPECT_EQ(journal_info.start_block, 8);
}

TEST(MinfsInspector, GetJournalEntryCount) {
  std::unique_ptr<MinfsInspector> inspector;
  SetupMinfsInspector(&inspector);
  Superblock sb = inspector->InspectSuperblock();
  uint64_t expected_count = JournalBlocks(sb) - fs::kJournalMetadataBlocks;
  EXPECT_EQ(inspector->GetJournalEntryCount(), expected_count);
}

// This ends up being a special case because we group both the journal superblock
// and the journal entries in a single vmo, so we cannot just naively subtract
// the number of superblocks from the size of the buffer in the case in which
// the buffer is uninitialized/have capacity of zero.
TEST(MinfsInspector, GetJournalEntryCountWithNoJournalBlocks) {
  std::unique_ptr<MinfsInspector> inspector;
  Superblock superblock = {};
  superblock.integrity_start_block = 0;
  superblock.dat_block = superblock.integrity_start_block + kBackupSuperblockBlocks;
  BadSetupMinfsInspector(&inspector, &superblock, sizeof(superblock));
  EXPECT_EQ(inspector->GetJournalEntryCount(), 0);
}

template <typename T>
void LoadAndUnwrapJournalEntry(MinfsInspector* inspector, uint64_t index, T* out_value) {
  auto result = inspector->InspectJournalEntryAs<T>(index);
  ASSERT_TRUE(result.is_ok());
  *out_value = result.take_value();
}

TEST(MinfsInspector, InspectJournalEntryAs) {
  std::unique_ptr<MinfsInspector> inspector;
  SetupMinfsInspector(&inspector);

  // First four entry blocks should be header, payload, payload, commit.
  fs::JournalHeaderBlock header;
  LoadAndUnwrapJournalEntry<fs::JournalHeaderBlock>(inspector.get(), 0, &header);
  EXPECT_EQ(header.prefix.magic, fs::kJournalEntryMagic);
  EXPECT_EQ(header.prefix.sequence_number, 0);
  EXPECT_EQ(header.prefix.flags, fs::kJournalPrefixFlagHeader);
  EXPECT_EQ(header.payload_blocks, 2);

  fs::JournalPrefix prefix;
  LoadAndUnwrapJournalEntry<fs::JournalPrefix>(inspector.get(), 1, &prefix);
  EXPECT_NE(prefix.magic, fs::kJournalEntryMagic);

  LoadAndUnwrapJournalEntry<fs::JournalPrefix>(inspector.get(), 2, &prefix);
  EXPECT_NE(prefix.magic, fs::kJournalEntryMagic);

  fs::JournalCommitBlock commit;
  LoadAndUnwrapJournalEntry<fs::JournalCommitBlock>(inspector.get(), 3, &commit);
  EXPECT_EQ(commit.prefix.magic, fs::kJournalEntryMagic);
  EXPECT_EQ(commit.prefix.sequence_number, 0);
  EXPECT_EQ(commit.prefix.flags, fs::kJournalPrefixFlagCommit);
}

TEST(MinfsInspector, InspectBackupSuperblock) {
  std::unique_ptr<MinfsInspector> inspector;
  SetupMinfsInspector(&inspector);

  auto result = inspector->InspectBackupSuperblock();
  ASSERT_TRUE(result.is_ok());
  Superblock sb = result.take_value();

  EXPECT_EQ(sb.magic0, kMinfsMagic0);
  EXPECT_EQ(sb.magic1, kMinfsMagic1);
  EXPECT_EQ(sb.format_version, kMinfsCurrentFormatVersion);
  EXPECT_EQ(sb.flags, kMinfsFlagClean);
  EXPECT_EQ(sb.block_size, kMinfsBlockSize);
  EXPECT_EQ(sb.inode_size, kMinfsInodeSize);
  EXPECT_EQ(sb.alloc_block_count, 2);
  EXPECT_EQ(sb.alloc_block_count, 2);
}

TEST(MinfsInspector, WriteSuperblock) {
  std::unique_ptr<MinfsInspector> inspector;
  SetupMinfsInspector(&inspector);
  Superblock sb = inspector->InspectSuperblock();
  // Test original values are correct.
  EXPECT_EQ(sb.magic0, kMinfsMagic0);
  EXPECT_EQ(sb.magic1, kMinfsMagic1);
  EXPECT_EQ(sb.format_version, kMinfsCurrentFormatVersion);

  // Edit values and write.
  sb.magic0 = 0;
  sb.format_version = 0;
  auto result = inspector->WriteSuperblock(sb);
  ASSERT_TRUE(result.is_ok());

  // Test if superblock is saved in memory.
  Superblock edit_sb = inspector->InspectSuperblock();
  EXPECT_EQ(edit_sb.magic0, 0);
  EXPECT_EQ(edit_sb.magic1, kMinfsMagic1);
  EXPECT_EQ(edit_sb.format_version, 0);

  // Test reloading from disk.
  ASSERT_OK(inspector->ReloadSuperblock());
  Superblock reload_sb = inspector->InspectSuperblock();
  EXPECT_EQ(reload_sb.magic0, 0);
  EXPECT_EQ(reload_sb.magic1, kMinfsMagic1);
  EXPECT_EQ(reload_sb.format_version, 0);
}

// TODO(fxbug.dev/46821): Implement these tests once we have a fake block device
// that can send proper error codes when bad operations are passed in.
// Currently if we send a read beyond device command, the block device
// itself will fail some test checks leading to this case being impossible to
// pass.
TEST(MinfsInspector, GracefulReadBeyondDevice) {}
TEST(MinfsInspector, GracefulReadFvmUnmappedData) {}

}  // namespace
}  // namespace minfs
