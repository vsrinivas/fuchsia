// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "inspector-inode-table.h"

#include <lib/disk-inspector/common-types.h>

#include "inspector-inode.h"
#include "inspector-private.h"

namespace minfs {

void InodeTableObject::GetValue(const void** out_buffer, size_t* out_buffer_size) const {
  ZX_DEBUG_ASSERT_MSG(false, "Invalid GetValue call for non primitive data type.");
}

std::unique_ptr<disk_inspector::DiskObject> InodeTableObject::GetElementAt(uint32_t index) const {
  if (index >= inode_count_) {
    return nullptr;
  }
  return GetInode(static_cast<ino_t>(index));
}

std::unique_ptr<disk_inspector::DiskObject> InodeTableObject::GetInode(ino_t inode) const {
  Inode inode_obj;
  inode_table_->Load(inode, &inode_obj);
  return std::unique_ptr<disk_inspector::DiskObject>(new InodeObject(inode, inode_obj));
}

}  // namespace minfs
