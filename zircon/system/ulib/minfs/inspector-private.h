// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file includes necessary methods for inspecting various on-disk structures
// of a MinFS filesystem.

#pragma once

#include <sys/stat.h>

#include <lib/disk-inspector/common-types.h>
#include <fbl/unique_ptr.h>
#include <fbl/unique_fd.h>
#include <minfs/format.h>

#include "allocator/inode-manager.h"
#include "minfs-private.h"

namespace minfs {

// Total number of elements present in root.
constexpr uint32_t kRootNumElements = 3;
constexpr char kRootName[] = "minfs-root";

// Total number of fields in the on-disk superblock structure.
constexpr uint32_t kSuperblockNumElements = 24;
constexpr char kSuperBlockName[] = "superblock";

// Total number of fields in the on-disk inode structure.
constexpr uint32_t kInodeNumElements = 15;
constexpr char kInodeName[] = "inode";

constexpr char kInodeTableName[] = "inode table";

// Total number of fields in the on-disk journal structure.
constexpr uint32_t kJournalNumElements = 5;
constexpr char kJournalName[] = "journal";

class InodeObject : public disk_inspector::DiskObject {
public:
    InodeObject() = delete;
    InodeObject(const InodeObject&)= delete;
    InodeObject(InodeObject&&) = delete;
    InodeObject& operator=(const InodeObject&) = delete;
    InodeObject& operator=(InodeObject&&) = delete;

    InodeObject(Inode& inode) : inode_(inode) {}

    // DiskObject interface:
    const char* GetName() const override {
        return kInodeName;
    }

    uint32_t GetNumElements() const override {
        return kInodeNumElements;
    }

    void GetValue(const void** out_buffer, size_t* out_buffer_size) const override;

    std::unique_ptr<DiskObject> GetElementAt(uint32_t index) const override;

private:
    // In-memory inode from the inode table.
    Inode inode_;
};

class InodeTableObject : public disk_inspector::DiskObject {
public:
    InodeTableObject() = delete;
    InodeTableObject(const InodeTableObject&)= delete;
    InodeTableObject(InodeTableObject&&) = delete;
    InodeTableObject& operator=(const InodeTableObject&) = delete;
    InodeTableObject& operator=(InodeTableObject&&) = delete;

    InodeTableObject(const InspectableInodeManager* inodes, const uint32_t inode_ct)
        : inode_table_(inodes), inode_count_(inode_ct) {}

    // DiskObject interface:
    const char* GetName() const override {
        return kInodeTableName;
    }

    uint32_t GetNumElements() const override {
        return inode_count_;
    }

    void GetValue(const void** out_buffer, size_t* out_buffer_size) const override;

    std::unique_ptr<DiskObject> GetElementAt(uint32_t index) const override;

private:
    // Gets inode DiskObject using the inode number |ino|.
    std::unique_ptr<disk_inspector::DiskObject> GetInode(ino_t inode) const;

    // Pointer to the minfs 'inodes_' field.
    const InspectableInodeManager* inode_table_;

    // Number of inodes allocated in the inode_table_.
    const uint32_t inode_count_;
};

class SuperBlockObject : public disk_inspector::DiskObject {
public:
    SuperBlockObject() = delete;
    SuperBlockObject(const SuperBlockObject&)= delete;
    SuperBlockObject(SuperBlockObject&&) = delete;
    SuperBlockObject& operator=(const SuperBlockObject&) = delete;
    SuperBlockObject& operator=(SuperBlockObject&&) = delete;

    SuperBlockObject(const Superblock &sb) : sb_(sb) {}

    // DiskObject interface:
    const char* GetName() const override {
        return kSuperBlockName;
    }

    uint32_t GetNumElements() const override {
        return kSuperblockNumElements;
    }

    void GetValue(const void** out_buffer, size_t* out_buffer_size) const override;

    std::unique_ptr<DiskObject> GetElementAt(uint32_t index) const override;

private:
    // minfs superblock object.
    const Superblock sb_;
};

class JournalObject : public disk_inspector::DiskObject {
public:
    JournalObject() = delete;
    JournalObject(const JournalObject&)= delete;
    JournalObject(JournalObject&&) = delete;
    JournalObject& operator=(const JournalObject&) = delete;
    JournalObject& operator=(JournalObject&&) = delete;

    JournalObject(std::unique_ptr<JournalInfo> info) : journal_info_(std::move(info)) {}

    // DiskObject interface:
    const char* GetName() const override {
        return kJournalName;
    }

    uint32_t GetNumElements() const override {
        return kJournalNumElements;
    }

    void GetValue(const void** out_buffer, size_t* out_buffer_size) const override;

    std::unique_ptr<DiskObject> GetElementAt(uint32_t index) const override;

private:
    // Name of DiskObject journal.
    fbl::String name_;

    // Pointer to the minfs journal info.
    std::unique_ptr<JournalInfo> journal_info_;
};

class RootObject : public disk_inspector::DiskObject {
public:
    RootObject() = delete;
    RootObject(const RootObject&)= delete;
    RootObject(RootObject&&) = delete;
    RootObject& operator=(const RootObject&) = delete;
    RootObject& operator=(RootObject&&) = delete;

    RootObject(std::unique_ptr<InspectableFilesystem> fs) : fs_(std::move(fs)) {}

   // DiskObject interface
    const char* GetName() const override {
        return kRootName;
    }

    uint32_t GetNumElements() const override {
        return kRootNumElements;
    }

    void GetValue(const void** out_buffer, size_t* out_buffer_size) const override;

    std::unique_ptr<DiskObject> GetElementAt(uint32_t index) const override;

private:
    // Gets the superblock diskObject element at index 0.
    std::unique_ptr<disk_inspector::DiskObject> GetSuperBlock() const;

    // Gets the inode_table_ diskObject element at index 1.
    std::unique_ptr<disk_inspector::DiskObject> GetInodeTable() const;

    // Gets the journal diskObject element at index 2.
    std::unique_ptr<disk_inspector::DiskObject> GetJournalInfo() const;

    // Pointer to the Minfs instance.
    std::unique_ptr<InspectableFilesystem> fs_;
};

} // namespace minfs
