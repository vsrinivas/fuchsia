// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MINFS_INSPECTOR_INODE_TABLE_H_
#define SRC_STORAGE_MINFS_INSPECTOR_INODE_TABLE_H_

#include <disk_inspector/common_types.h>
#include <fs/journal/format.h>
#include <minfs/format.h>

#include "allocator/inode_manager.h"
#include "minfs_private.h"

namespace minfs {

constexpr char kInodeTableName[] = "inode table";

// The current implementation of the InodeTableObject prints out every inode
// that is allocated in the inode table.
// TODO(fxb/37907): Change this implementation once we have a better format
// for how to display inodes when making disk-inspect interactive.
// Refer to (fxb/39660) for more details about the current implementation.
class InodeTableObject : public disk_inspector::DiskObject {
 public:
  InodeTableObject() = delete;
  InodeTableObject(const InodeTableObject&) = delete;
  InodeTableObject(InodeTableObject&&) = delete;
  InodeTableObject& operator=(const InodeTableObject&) = delete;
  InodeTableObject& operator=(InodeTableObject&&) = delete;

  InodeTableObject(const InspectableInodeManager* inodes, uint32_t allocated_inode_count,
                   uint32_t inode_count)
      : inode_table_(inodes),
        allocated_inode_count_(allocated_inode_count),
        inode_count_(inode_count) {
    SetupAllocatedInodeIndex();
  }

  // DiskObject interface:
  const char* GetName() const override { return kInodeTableName; }

  uint32_t GetNumElements() const override { return allocated_inode_count_; }

  void GetValue(const void** out_buffer, size_t* out_buffer_size) const override;

  std::unique_ptr<DiskObject> GetElementAt(uint32_t index) const override;

 private:
  // Gets the inode from the list of allocated inodes at index |element_index|.
  std::unique_ptr<disk_inspector::DiskObject> GetInode(uint32_t element_index) const;

  void SetupAllocatedInodeIndex();

  // Pointer to the minfs 'inodes_' field.
  const InspectableInodeManager* inode_table_;
  // Number of allocated inodes in the inode table.
  const uint32_t allocated_inode_count_;
  // Total number of inodes in the inode table.
  const uint32_t inode_count_;
  // List of indices of allocated inodes in the inode table.
  std::vector<uint32_t> allocated_inode_indices;
};

}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_INSPECTOR_INODE_TABLE_H_
