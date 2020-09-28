// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MINFS_INSPECTOR_INODE_H_
#define SRC_STORAGE_MINFS_INSPECTOR_INODE_H_

#include <disk_inspector/common_types.h>
#include <fbl/string_printf.h>
#include <fs/journal/format.h>

#include "src/storage/minfs/format.h"
#include "src/storage/minfs/minfs_private.h"

namespace minfs {

// Total number of fields in the on-disk inode structure.
constexpr uint32_t kInodeNumElements = 16;

class InodeObject : public disk_inspector::DiskObject {
 public:
  InodeObject() = delete;
  InodeObject(const InodeObject&) = delete;
  InodeObject(InodeObject&&) = delete;
  InodeObject& operator=(const InodeObject&) = delete;
  InodeObject& operator=(InodeObject&&) = delete;

  InodeObject(uint32_t allocated_inode_index, uint32_t inode_index, Inode inode)
      : allocated_inode_index_(allocated_inode_index),
        inode_index_(inode_index),
        inode_(inode),
        name_(fbl::StringPrintf("allocated #%d, inode #%d", allocated_inode_index_, inode_index_)) {
  }

  // DiskObject interface:
  const char* GetName() const override { return name_.c_str(); }

  uint32_t GetNumElements() const override { return kInodeNumElements; }

  void GetValue(const void** out_buffer, size_t* out_buffer_size) const override;

  std::unique_ptr<DiskObject> GetElementAt(uint32_t index) const override;

 private:
  // Index of inode in list of only allocated inodes in inode table.
  const uint32_t allocated_inode_index_;
  // Position of inode in the inode table.
  const uint32_t inode_index_;
  const Inode inode_;
  // TODO(fxbug.dev/37907): Currently the name is in the format "allocated #, inode #".
  // We should change this once disk-inspect does not index based on allocations
  // and rather the actual inode table index.
  const fbl::String name_;
};

}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_INSPECTOR_INODE_H_
