// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_MINFS_INSPECTOR_INODE_H_
#define ZIRCON_SYSTEM_ULIB_MINFS_INSPECTOR_INODE_H_

#include <lib/disk-inspector/common-types.h>

#include <fbl/string_printf.h>
#include <fs/journal/format.h>
#include <minfs/format.h>

#include "minfs-private.h"

namespace minfs {

// Total number of fields in the on-disk inode structure.
constexpr uint32_t kInodeNumElements = 15;
constexpr char kInodeName[] = "inode";

class InodeObject : public disk_inspector::DiskObject {
 public:
  InodeObject() = delete;
  InodeObject(const InodeObject&) = delete;
  InodeObject(InodeObject&&) = delete;
  InodeObject& operator=(const InodeObject&) = delete;
  InodeObject& operator=(InodeObject&&) = delete;

  InodeObject(ino_t ino, Inode inode)
    : ino_(ino), inode_(inode), name_(fbl::StringPrintf("%s #%d", kInodeName, ino_)) {}

  // DiskObject interface:
  const char* GetName() const override { return name_.c_str(); }

  uint32_t GetNumElements() const override { return kInodeNumElements; }

  void GetValue(const void** out_buffer, size_t* out_buffer_size) const override;

  std::unique_ptr<DiskObject> GetElementAt(uint32_t index) const override;

 private:
  // In-memory inode from the inode table.
  const ino_t ino_;
  const Inode inode_;
  const fbl::String name_;
};

}  // namespace minfs

#endif  // ZIRCON_SYSTEM_ULIB_MINFS_INSPECTOR_INODE_H_
