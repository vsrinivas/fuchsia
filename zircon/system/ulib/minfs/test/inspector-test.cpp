// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests minfs inspector behavior.

#include <minfs/inspector.h>
#include "minfs-private.h"
#include "inspector-private.h"
#include <lib/disk-inspector/disk-inspector.h>
#include <zxtest/zxtest.h>

namespace minfs {
namespace {

// Mock InodeManager class to be used in inspector tests.
class MockInodeManager : public InspectableInodeManager {
public:
    MockInodeManager();
    MockInodeManager(const MockInodeManager&)= delete;
    MockInodeManager(MockInodeManager&&) = delete;
    MockInodeManager& operator=(const MockInodeManager&) = delete;
    MockInodeManager& operator=(MockInodeManager&&) = delete;

    void Load(ino_t inode_num, Inode* out) const final;
    const Allocator* GetInodeAllocator() const final;
};

MockInodeManager::MockInodeManager() {}

void MockInodeManager::Load(ino_t inode_num, Inode* out) const {}

const Allocator* MockInodeManager::GetInodeAllocator() const {
    return nullptr;
}

constexpr Superblock superblock = {};

// Mock Minfs class to be used in inspector tests.
class MockMinfs : public InspectableFilesystem {
public:
    MockMinfs() = default;
    MockMinfs(const MockMinfs&)= delete;
    MockMinfs(MockMinfs&&) = delete;
    MockMinfs& operator=(const MockMinfs&) = delete;
    MockMinfs& operator=(MockMinfs&&) = delete;

    const Superblock& Info() const {
        return superblock;
    }

    const InspectableInodeManager* GetInodeManager() const final {
        return nullptr;
    }

    const Allocator* GetBlockAllocator() const final {
        return nullptr;
    }

    zx_status_t ReadBlock(blk_t start_block_num, void* out_data) const final {
        return ZX_OK;
    }
};

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
    ASSERT_STR_EQ(kJournalName, obj2->GetName());
    ASSERT_EQ(kJournalNumElements, obj2->GetNumElements());
}

TEST(InspectorTest, TestInodeTable) {
    auto inode_table_obj = std::unique_ptr<MockInodeManager>(new MockInodeManager());

    std::unique_ptr<InodeTableObject> inode_mgr(new InodeTableObject(inode_table_obj.get(), 2));
    ASSERT_STR_EQ(kInodeTableName, inode_mgr->GetName());
    ASSERT_EQ(2, inode_mgr->GetNumElements());

    std::unique_ptr<disk_inspector::DiskObject> obj0 = inode_mgr->GetElementAt(0);
    ASSERT_STR_EQ(kInodeName, obj0->GetName());
    ASSERT_EQ(kInodeNumElements, obj0->GetNumElements());

    std::unique_ptr<disk_inspector::DiskObject> obj1 = inode_mgr->GetElementAt(1);
    ASSERT_STR_EQ(kInodeName, obj1->GetName());
    ASSERT_EQ(kInodeNumElements, obj1->GetNumElements());
}

TEST(InspectorTest, TestSuperblock) {
    Superblock sb;
    sb.magic0 = kMinfsMagic0;
    sb.magic1 = kMinfsMagic1;
    sb.version = kMinfsVersion;
    sb.flags = kMinfsFlagClean;
    sb.block_size = kMinfsBlockSize;
    sb.inode_size = kMinfsInodeSize;

    size_t size;
    const void* buffer = nullptr;

    std::unique_ptr<SuperBlockObject> superblock(new SuperBlockObject(sb));
    ASSERT_STR_EQ(kSuperBlockName, superblock->GetName());
    ASSERT_EQ(kSuperblockNumElements, superblock->GetNumElements());

    std::unique_ptr<disk_inspector::DiskObject> obj0 = superblock->GetElementAt(0);
    obj0->GetValue(&buffer, &size);
    ASSERT_EQ(kMinfsMagic0, *(reinterpret_cast<const uint64_t*>(buffer)));

    std::unique_ptr<disk_inspector::DiskObject> obj1 = superblock->GetElementAt(1);
    obj1->GetValue(&buffer, &size);
    ASSERT_EQ(kMinfsMagic1, *(reinterpret_cast<const uint64_t*>(buffer)));

    std::unique_ptr<disk_inspector::DiskObject> obj2 = superblock->GetElementAt(2);
    obj2->GetValue(&buffer, &size);
    ASSERT_EQ(kMinfsVersion, *(reinterpret_cast<const uint32_t*>(buffer)));

    std::unique_ptr<disk_inspector::DiskObject> obj3 = superblock->GetElementAt(3);
    obj3->GetValue(&buffer, &size);
    ASSERT_EQ(kMinfsFlagClean, *(reinterpret_cast<const uint32_t*>(buffer)));

    std::unique_ptr<disk_inspector::DiskObject> obj4 = superblock->GetElementAt(4);
    obj4->GetValue(&buffer, &size);
    ASSERT_EQ(kMinfsBlockSize, *(reinterpret_cast<const uint32_t*>(buffer)));

    std::unique_ptr<disk_inspector::DiskObject> obj5 = superblock->GetElementAt(5);
    obj5->GetValue(&buffer, &size);
    ASSERT_EQ(kMinfsInodeSize, *(reinterpret_cast<const uint32_t*>(buffer)));
}

TEST(InspectorTest, TestJournal) {
    JournalInfo jinfo;
    jinfo.magic = kJournalMagic;
    auto info = std::make_unique <JournalInfo>(jinfo);

    std::unique_ptr<JournalObject> journalObj(new JournalObject(std::move(info)));
    ASSERT_STR_EQ(kJournalName, journalObj->GetName());
    ASSERT_EQ(kJournalNumElements, journalObj->GetNumElements());

    size_t size;
    const void* buffer = nullptr;

    std::unique_ptr<disk_inspector::DiskObject> obj0 = journalObj->GetElementAt(0);
    obj0->GetValue(&buffer, &size);
    ASSERT_EQ(kJournalMagic, *(reinterpret_cast<const uint64_t*>(buffer)));
}

TEST(InspectorTest, TestInode) {
    Inode fileInode;
    fileInode.magic = kMinfsMagicFile;
    fileInode.size = 10;
    fileInode.block_count = 2;
    fileInode.link_count = 1;

    std::unique_ptr<InodeObject> finodeObj(new InodeObject(fileInode));
    ASSERT_STR_EQ(kInodeName, finodeObj->GetName());
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

} // namespace
} // namespace minfs
