// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_MINFS_INSPECTOR_INODE_TABLE_H_
#define ZIRCON_SYSTEM_ULIB_MINFS_INSPECTOR_INODE_TABLE_H_

#include <lib/disk-inspector/common-types.h>

#include <fbl/unique_ptr.h>
#include <fs/journal/format.h>
#include <minfs/format.h>

#include "allocator/inode-manager.h"
#include "minfs-private.h"

namespace minfs {

constexpr char kInodeTableName[] = "inode table";

class InodeTableObject : public disk_inspector::DiskObject {
 public:
  InodeTableObject() = delete;
  InodeTableObject(const InodeTableObject&) = delete;
  InodeTableObject(InodeTableObject&&) = delete;
  InodeTableObject& operator=(const InodeTableObject&) = delete;
  InodeTableObject& operator=(InodeTableObject&&) = delete;

  InodeTableObject(const InspectableInodeManager* inodes, const uint32_t inode_ct)
      : inode_table_(inodes), inode_count_(inode_ct) {}

  // DiskObject interface:
  const char* GetName() const override { return kInodeTableName; }

  uint32_t GetNumElements() const override { return inode_count_; }

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

}  // namespace minfs

#endif  // ZIRCON_SYSTEM_ULIB_MINFS_INSPECTOR_INODE_TABLE_H_
