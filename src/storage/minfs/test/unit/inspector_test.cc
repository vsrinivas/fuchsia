// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests minfs inspector behavior.

#include "src/storage/minfs/inspector.h"

#include <lib/sync/completion.h>

#include <block-client/cpp/fake-device.h>
#include <disk_inspector/disk_inspector.h>
#include <fbl/string_printf.h>
#include <fs/journal/inspector_journal.h>
#include <gtest/gtest.h>

#include "src/storage/minfs/inspector_inode.h"
#include "src/storage/minfs/inspector_inode_table.h"
#include "src/storage/minfs/inspector_private.h"
#include "src/storage/minfs/inspector_superblock.h"
#include "src/storage/minfs/minfs_private.h"

namespace minfs {
namespace {

using block_client::FakeBlockDevice;

constexpr uint64_t kBlockCount = 1 << 15;
constexpr uint32_t kBlockSize = 512;

// Mock InodeManager class to be used in inspector tests.
class MockInodeManager : public InspectableInodeManager {
 public:
  MockInodeManager();
  MockInodeManager(const MockInodeManager&) = delete;
  MockInodeManager(MockInodeManager&&) = delete;
  MockInodeManager& operator=(const MockInodeManager&) = delete;
  MockInodeManager& operator=(MockInodeManager&&) = delete;

  void Load(ino_t inode_num, Inode* out) const final;
  bool CheckAllocated(uint32_t inode_num) const final;
  const Allocator* GetInodeAllocator() const final;
};

MockInodeManager::MockInodeManager() {}

void MockInodeManager::Load(ino_t inode_num, Inode* out) const {}

// We fake that only the inode at index 1 is allocated.
bool MockInodeManager::CheckAllocated(uint32_t inode_num) const { return (inode_num == 1); }

const Allocator* MockInodeManager::GetInodeAllocator() const { return nullptr; }

constexpr Superblock superblock = {};

// Mock Minfs class to be used in inspector tests.
class MockMinfs : public InspectableMinfs {
 public:
  MockMinfs() = default;
  MockMinfs(const MockMinfs&) = delete;
  MockMinfs(MockMinfs&&) = delete;
  MockMinfs& operator=(const MockMinfs&) = delete;
  MockMinfs& operator=(MockMinfs&&) = delete;

  const Superblock& Info() const { return superblock; }

  const InspectableInodeManager* GetInodeManager() const final { return nullptr; }

  const Allocator& GetBlockAllocator() const final { ZX_ASSERT(false); }

  zx_status_t ReadBlock(blk_t start_block_num, void* out_data) const final { return ZX_OK; }
};

uint64_t GetUint64Value(const disk_inspector::DiskObject* object) {
  size_t size;
  const void* buffer = nullptr;
  object->GetValue(&buffer, &size);

  if (size != sizeof(uint64_t)) {
    ADD_FAILURE() << "Unexpected value size";
    return 0;
  }
  return *reinterpret_cast<const uint64_t*>(buffer);
}

void RunSuperblockTest(SuperblockType version) {
  Superblock sb;
  sb.magic0 = kMinfsMagic0;
  sb.magic1 = kMinfsMagic1;
  sb.format_version = kMinfsCurrentFormatVersion;
  sb.flags = kMinfsFlagClean;
  sb.block_size = kMinfsBlockSize;
  sb.inode_size = kMinfsInodeSize;
  sb.oldest_revision = kMinfsCurrentRevision;

  size_t size;
  const void* buffer = nullptr;

  auto superblock = std::make_unique<SuperBlockObject>(sb, version);
  switch (version) {
    case SuperblockType::kPrimary:
      ASSERT_EQ(superblock->GetName(), std::string_view(kSuperBlockName));
      break;
    case SuperblockType::kBackup:
      ASSERT_EQ(superblock->GetName(), std::string_view(kBackupSuperBlockName));
      break;
    default:
      FAIL() << "Unexpected superblock type";
  }
  ASSERT_EQ(kSuperblockNumElements, superblock->GetNumElements());

  std::unique_ptr<disk_inspector::DiskObject> obj0 = superblock->GetElementAt(0);
  obj0->GetValue(&buffer, &size);
  ASSERT_EQ(kMinfsMagic0, *(reinterpret_cast<const uint64_t*>(buffer)));

  std::unique_ptr<disk_inspector::DiskObject> obj1 = superblock->GetElementAt(1);
  obj1->GetValue(&buffer, &size);
  ASSERT_EQ(kMinfsMagic1, *(reinterpret_cast<const uint64_t*>(buffer)));

  std::unique_ptr<disk_inspector::DiskObject> obj2 = superblock->GetElementAt(2);
  obj2->GetValue(&buffer, &size);
  ASSERT_EQ(kMinfsCurrentFormatVersion, *(reinterpret_cast<const uint32_t*>(buffer)));

  std::unique_ptr<disk_inspector::DiskObject> obj4 = superblock->GetElementAt(3);
  obj4->GetValue(&buffer, &size);
  ASSERT_EQ(kMinfsFlagClean, *(reinterpret_cast<const uint32_t*>(buffer)));

  std::unique_ptr<disk_inspector::DiskObject> obj5 = superblock->GetElementAt(4);
  obj5->GetValue(&buffer, &size);
  ASSERT_EQ(kMinfsBlockSize, *(reinterpret_cast<const uint32_t*>(buffer)));

  std::unique_ptr<disk_inspector::DiskObject> obj6 = superblock->GetElementAt(5);
  obj6->GetValue(&buffer, &size);
  ASSERT_EQ(kMinfsInodeSize, *(reinterpret_cast<const uint32_t*>(buffer)));
}

TEST(InspectorTest, TestRoot) {
  auto fs = std::unique_ptr<MockMinfs>(new MockMinfs());

  std::unique_ptr<RootObject> root_obj(new RootObject(std::move(fs)));
  ASSERT_EQ(root_obj->GetName(), std::string_view(kRootName));
  ASSERT_EQ(kRootNumElements, root_obj->GetNumElements());

  // Superblock.
  std::unique_ptr<disk_inspector::DiskObject> obj0 = root_obj->GetElementAt(0);
  ASSERT_EQ(obj0->GetName(), std::string_view(kSuperBlockName));
  ASSERT_EQ(kSuperblockNumElements, obj0->GetNumElements());

  // Inode Table.
  std::unique_ptr<disk_inspector::DiskObject> obj1 = root_obj->GetElementAt(1);
  ASSERT_EQ(obj1->GetName(), std::string_view(kInodeTableName));

  // Journal info.
  std::unique_ptr<disk_inspector::DiskObject> obj2 = root_obj->GetElementAt(2);
  ASSERT_EQ(obj2->GetName(), std::string_view(fs::kJournalName));
  ASSERT_EQ(fs::kJournalNumElements, obj2->GetNumElements());
}

TEST(InspectorTest, TestInodeTable) {
  auto inode_table_obj = std::unique_ptr<MockInodeManager>(new MockInodeManager());

  uint32_t allocated_num = 1;
  uint32_t inode_num = 3;
  std::unique_ptr<InodeTableObject> inode_mgr(
      new InodeTableObject(inode_table_obj.get(), allocated_num, inode_num));
  ASSERT_EQ(inode_mgr->GetName(), std::string_view(kInodeTableName));
  ASSERT_EQ(allocated_num, inode_mgr->GetNumElements());

  // The only allocated inode should be inode #1 as defined in |MockInodeManager::CheckAllocated|.
  std::unique_ptr<disk_inspector::DiskObject> obj0 = inode_mgr->GetElementAt(0);
  fbl::String name = fbl::StringPrintf("allocated #%d, inode #%d", 0, 1);
  ASSERT_EQ(obj0->GetName(), name);
  ASSERT_EQ(kInodeNumElements, obj0->GetNumElements());
}

TEST(InspectorTest, TestSuperblock) { RunSuperblockTest(SuperblockType::kPrimary); }

TEST(InspectorTest, TestInode) {
  Inode fileInode;
  fileInode.magic = kMinfsMagicFile;
  fileInode.size = 10;
  fileInode.block_count = 2;
  fileInode.link_count = 1;

  uint32_t allocated_num = 2;
  uint32_t inode_num = 4;
  std::unique_ptr<InodeObject> finodeObj(new InodeObject(allocated_num, inode_num, fileInode));
  fbl::String name = fbl::StringPrintf("allocated #%d, inode #%d", allocated_num, inode_num);
  ASSERT_EQ(finodeObj->GetName(), name);
  ASSERT_EQ(kInodeNumElements, finodeObj->GetNumElements());

  size_t size;
  const void* buffer = nullptr;

  std::unique_ptr<disk_inspector::DiskObject> obj0 = finodeObj->GetElementAt(0);
  obj0->GetValue(&buffer, &size);
  ASSERT_EQ(kMinfsMagicFile, *(reinterpret_cast<const uint32_t*>(buffer)));

  std::unique_ptr<disk_inspector::DiskObject> obj1 = finodeObj->GetElementAt(1);
  obj1->GetValue(&buffer, &size);
  ASSERT_EQ(*(reinterpret_cast<const uint32_t*>(buffer)), 10u);

  std::unique_ptr<disk_inspector::DiskObject> obj2 = finodeObj->GetElementAt(2);
  obj2->GetValue(&buffer, &size);
  ASSERT_EQ(*(reinterpret_cast<const uint32_t*>(buffer)), 2u);

  std::unique_ptr<disk_inspector::DiskObject> obj3 = finodeObj->GetElementAt(3);
  obj3->GetValue(&buffer, &size);
  ASSERT_EQ(*(reinterpret_cast<const uint32_t*>(buffer)), 1u);
}

TEST(InspectorTest, CorrectJournalLocation) {
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);

  // Format the device.
  std::unique_ptr<Bcache> bcache;
  ASSERT_EQ(Bcache::Create(std::move(device), kBlockCount, &bcache), ZX_OK);
  ASSERT_EQ(Mkfs(bcache.get()), ZX_OK);

  std::unique_ptr<Minfs> fs;
  MountOptions options = {};
  ASSERT_EQ(minfs::Minfs::Create(std::move(bcache), options, &fs), ZX_OK);

  // Ensure the dirty bit is propagated to the device.
  sync_completion_t completion;
  fs->Sync([&completion](zx_status_t status) { sync_completion_signal(&completion); });
  ASSERT_EQ(sync_completion_wait(&completion, zx::duration::infinite().get()), ZX_OK);

  uint64_t journal_length = JournalBlocks(fs->Info());
  std::unique_ptr<RootObject> root_obj(new RootObject(std::move(fs)));

  // Journal info.
  auto journalObj = root_obj->GetElementAt(2);
  EXPECT_EQ(journalObj->GetName(), std::string_view(fs::kJournalName));
  ASSERT_EQ(fs::kJournalNumElements, journalObj->GetNumElements());

  // Check if journal magic is correct.
  auto journalMagic = journalObj->GetElementAt(0);
  ASSERT_EQ(fs::kJournalMagic, GetUint64Value(journalMagic.get()));

  // Access journal entries.
  auto entries = journalObj->GetElementAt(5);
  EXPECT_EQ(entries->GetName(), std::string_view(fs::kJournalEntriesName));
  ASSERT_EQ(journal_length - fs::kJournalMetadataBlocks, entries->GetNumElements());

  // Parse the header block.
  //
  // Warning: This has tight coupling with the dirty bit and backup superblock.
  // To ensure this exists on the journal, we invoked sync earlier in the test.
  auto block = entries->GetElementAt(0);
  EXPECT_EQ(block->GetName(), std::string_view("Journal[0]: Header"));
  {
    auto entryMagic = block->GetElementAt(0);
    EXPECT_EQ(entryMagic->GetName(), std::string_view("magic"));
    ASSERT_EQ(fs::kJournalEntryMagic, GetUint64Value(entryMagic.get()));

    auto payload_blocks = block->GetElementAt(4);
    EXPECT_EQ(payload_blocks->GetName(), std::string_view("payload blocks"));
    ASSERT_EQ(GetUint64Value(payload_blocks.get()), 2ul);

    auto target_block = block->GetElementAt(5);
    EXPECT_EQ(target_block->GetName(), std::string_view("target block"));
    EXPECT_EQ(kSuperblockStart, GetUint64Value(target_block.get()));

    target_block = block->GetElementAt(6);
    EXPECT_EQ(target_block->GetName(), std::string_view("target block"));
    EXPECT_EQ(kNonFvmSuperblockBackup, GetUint64Value(target_block.get()));

    EXPECT_EQ(block->GetElementAt(7), nullptr);
  }

  // Parse the journal entries.
  block = entries->GetElementAt(1);
  EXPECT_EQ(block->GetName(), std::string_view("Journal[1]: Block"));

  block = entries->GetElementAt(2);
  EXPECT_EQ(block->GetName(), std::string_view("Journal[2]: Block"));

  // Parse the commit block.
  block = entries->GetElementAt(3);
  EXPECT_EQ(block->GetName(), std::string_view("Journal[3]: Commit"));
}

// Currently, the only difference between this test and TestSuperblock is that
// this returns a different name.
TEST(InspectorTest, TestBackupSuperblock) { RunSuperblockTest(SuperblockType::kBackup); }

}  // namespace
}  // namespace minfs
