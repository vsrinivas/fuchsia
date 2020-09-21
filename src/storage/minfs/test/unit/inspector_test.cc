// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests minfs inspector behavior.

#include <lib/sync/completion.h>

#include <block-client/cpp/fake-device.h>
#include <disk_inspector/disk_inspector.h>
#include <fbl/string_printf.h>
#include <fs/journal/inspector_journal.h>
#include <minfs/inspector.h>
#include <zxtest/zxtest.h>

#include "inspector_inode.h"
#include "inspector_inode_table.h"
#include "inspector_private.h"
#include "inspector_superblock.h"
#include "minfs_private.h"

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
    ADD_FAILURE("Unexpected value size");
    return 0;
  }
  return *reinterpret_cast<const uint64_t*>(buffer);
}

void RunSuperblockTest(SuperblockType version) {
  Superblock sb;
  sb.magic0 = kMinfsMagic0;
  sb.magic1 = kMinfsMagic1;
  sb.version_major = kMinfsMajorVersion;
  sb.version_minor = kMinfsMinorVersion;
  sb.flags = kMinfsFlagClean;
  sb.block_size = kMinfsBlockSize;
  sb.inode_size = kMinfsInodeSize;

  size_t size;
  const void* buffer = nullptr;

  std::unique_ptr<SuperBlockObject> superblock(new SuperBlockObject(sb, version));
  switch (version) {
    case SuperblockType::kPrimary:
      ASSERT_STR_EQ(kSuperBlockName, superblock->GetName());
      break;
    case SuperblockType::kBackup:
      ASSERT_STR_EQ(kBackupSuperBlockName, superblock->GetName());
      break;
    default:
      ADD_FATAL_FAILURE("Unexpected superblock type");
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
  ASSERT_EQ(kMinfsMajorVersion, *(reinterpret_cast<const uint32_t*>(buffer)));

  std::unique_ptr<disk_inspector::DiskObject> obj3 = superblock->GetElementAt(3);
  obj3->GetValue(&buffer, &size);
  ASSERT_EQ(kMinfsMinorVersion, *(reinterpret_cast<const uint32_t*>(buffer)));

  std::unique_ptr<disk_inspector::DiskObject> obj4 = superblock->GetElementAt(4);
  obj4->GetValue(&buffer, &size);
  ASSERT_EQ(kMinfsFlagClean, *(reinterpret_cast<const uint32_t*>(buffer)));

  std::unique_ptr<disk_inspector::DiskObject> obj5 = superblock->GetElementAt(5);
  obj5->GetValue(&buffer, &size);
  ASSERT_EQ(kMinfsBlockSize, *(reinterpret_cast<const uint32_t*>(buffer)));

  std::unique_ptr<disk_inspector::DiskObject> obj6 = superblock->GetElementAt(6);
  obj6->GetValue(&buffer, &size);
  ASSERT_EQ(kMinfsInodeSize, *(reinterpret_cast<const uint32_t*>(buffer)));
}

TEST(InspectorTest, TestRoot) {
  auto fs = std::unique_ptr<MockMinfs>(new MockMinfs());

  std::unique_ptr<RootObject> root_obj(new RootObject(std::move(fs)));
  ASSERT_STR_EQ(kRootName, root_obj->GetName());
  ASSERT_EQ(kRootNumElements, root_obj->GetNumElements());

  // Superblock.
  std::unique_ptr<disk_inspector::DiskObject> obj0 = root_obj->GetElementAt(0);
  ASSERT_STR_EQ(kSuperBlockName, obj0->GetName());
  ASSERT_EQ(kSuperblockNumElements, obj0->GetNumElements());

  // Inode Table.
  std::unique_ptr<disk_inspector::DiskObject> obj1 = root_obj->GetElementAt(1);
  ASSERT_STR_EQ(kInodeTableName, obj1->GetName());

  // Journal info.
  std::unique_ptr<disk_inspector::DiskObject> obj2 = root_obj->GetElementAt(2);
  ASSERT_STR_EQ(fs::kJournalName, obj2->GetName());
  ASSERT_EQ(fs::kJournalNumElements, obj2->GetNumElements());
}

TEST(InspectorTest, TestInodeTable) {
  auto inode_table_obj = std::unique_ptr<MockInodeManager>(new MockInodeManager());

  uint32_t allocated_num = 1;
  uint32_t inode_num = 3;
  std::unique_ptr<InodeTableObject> inode_mgr(
      new InodeTableObject(inode_table_obj.get(), allocated_num, inode_num));
  ASSERT_STR_EQ(kInodeTableName, inode_mgr->GetName());
  ASSERT_EQ(allocated_num, inode_mgr->GetNumElements());

  // The only allocated inode should be inode #1 as defined in |MockInodeManager::CheckAllocated|.
  std::unique_ptr<disk_inspector::DiskObject> obj0 = inode_mgr->GetElementAt(0);
  fbl::String name = fbl::StringPrintf("allocated #%d, inode #%d", 0, 1);
  ASSERT_STR_EQ(name, obj0->GetName());
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
  ASSERT_STR_EQ(name, finodeObj->GetName());
  ASSERT_EQ(kInodeNumElements, finodeObj->GetNumElements());

  size_t size;
  const void* buffer = nullptr;

  std::unique_ptr<disk_inspector::DiskObject> obj0 = finodeObj->GetElementAt(0);
  obj0->GetValue(&buffer, &size);
  ASSERT_EQ(kMinfsMagicFile, *(reinterpret_cast<const uint32_t*>(buffer)));

  std::unique_ptr<disk_inspector::DiskObject> obj1 = finodeObj->GetElementAt(1);
  obj1->GetValue(&buffer, &size);
  ASSERT_EQ(10, *(reinterpret_cast<const uint32_t*>(buffer)));

  std::unique_ptr<disk_inspector::DiskObject> obj2 = finodeObj->GetElementAt(2);
  obj2->GetValue(&buffer, &size);
  ASSERT_EQ(2, *(reinterpret_cast<const uint32_t*>(buffer)));

  std::unique_ptr<disk_inspector::DiskObject> obj3 = finodeObj->GetElementAt(3);
  obj3->GetValue(&buffer, &size);
  ASSERT_EQ(1, *(reinterpret_cast<const uint32_t*>(buffer)));
}

TEST(InspectorTest, CorrectJournalLocation) {
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);

  // Format the device.
  std::unique_ptr<Bcache> bcache;
  ASSERT_OK(Bcache::Create(std::move(device), kBlockCount, &bcache));
  ASSERT_OK(Mkfs(bcache.get()));

  std::unique_ptr<Minfs> fs;
  MountOptions options = {};
  ASSERT_OK(minfs::Minfs::Create(std::move(bcache), options, &fs));

  // Ensure the dirty bit is propagated to the device.
  sync_completion_t completion;
  fs->Sync([&completion](zx_status_t status) { sync_completion_signal(&completion); });
  ASSERT_OK(sync_completion_wait(&completion, zx::duration::infinite().get()));

  uint64_t journal_length = JournalBlocks(fs->Info());
  std::unique_ptr<RootObject> root_obj(new RootObject(std::move(fs)));

  // Journal info.
  auto journalObj = root_obj->GetElementAt(2);
  EXPECT_STR_EQ(fs::kJournalName, journalObj->GetName());
  ASSERT_EQ(fs::kJournalNumElements, journalObj->GetNumElements());

  // Check if journal magic is correct.
  auto journalMagic = journalObj->GetElementAt(0);
  ASSERT_EQ(fs::kJournalMagic, GetUint64Value(journalMagic.get()));

  // Access journal entries.
  auto entries = journalObj->GetElementAt(5);
  EXPECT_STR_EQ(fs::kJournalEntriesName, entries->GetName());
  ASSERT_EQ(journal_length - fs::kJournalMetadataBlocks, entries->GetNumElements());

  // Parse the header block.
  //
  // Warning: This has tight coupling with the dirty bit and backup superblock.
  // To ensure this exists on the journal, we invoked sync earlier in the test.
  auto block = entries->GetElementAt(0);
  EXPECT_STR_EQ("Journal[0]: Header", block->GetName());
  {
    auto entryMagic = block->GetElementAt(0);
    EXPECT_STR_EQ("magic", entryMagic->GetName());
    ASSERT_EQ(fs::kJournalEntryMagic, GetUint64Value(entryMagic.get()));

    auto payload_blocks = block->GetElementAt(4);
    EXPECT_STR_EQ("payload blocks", payload_blocks->GetName());
    ASSERT_EQ(2, GetUint64Value(payload_blocks.get()));

    auto target_block = block->GetElementAt(5);
    EXPECT_STR_EQ("target block", target_block->GetName());
    EXPECT_EQ(kSuperblockStart, GetUint64Value(target_block.get()));

    target_block = block->GetElementAt(6);
    EXPECT_STR_EQ("target block", target_block->GetName());
    EXPECT_EQ(kNonFvmSuperblockBackup, GetUint64Value(target_block.get()));

    EXPECT_NULL(block->GetElementAt(7));
  }

  // Parse the journal entries.
  block = entries->GetElementAt(1);
  EXPECT_STR_EQ("Journal[1]: Block", block->GetName());

  block = entries->GetElementAt(2);
  EXPECT_STR_EQ("Journal[2]: Block", block->GetName());

  // Parse the commit block.
  block = entries->GetElementAt(3);
  EXPECT_STR_EQ("Journal[3]: Commit", block->GetName());
}

// Currently, the only difference between this test and TestSuperblock is that
// this returns a different name.
TEST(InspectorTest, TestBackupSuperblock) { RunSuperblockTest(SuperblockType::kBackup); }

}  // namespace
}  // namespace minfs
