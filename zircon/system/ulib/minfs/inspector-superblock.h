// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_MINFS_INSPECTOR_SUPERBLOCK_H_
#define ZIRCON_SYSTEM_ULIB_MINFS_INSPECTOR_SUPERBLOCK_H_

#include <lib/disk-inspector/common-types.h>

#include <fbl/unique_ptr.h>
#include <fs/journal/format.h>
#include <minfs/format.h>

#include "allocator/inode-manager.h"
#include "minfs-private.h"

namespace minfs {

// Total number of fields in the on-disk superblock structure.
constexpr uint32_t kSuperblockNumElements = 28;
constexpr char kSuperBlockName[] = "superblock";

class SuperBlockObject : public disk_inspector::DiskObject {
 public:
  SuperBlockObject() = delete;
  SuperBlockObject(const SuperBlockObject&) = delete;
  SuperBlockObject(SuperBlockObject&&) = delete;
  SuperBlockObject& operator=(const SuperBlockObject&) = delete;
  SuperBlockObject& operator=(SuperBlockObject&&) = delete;

  SuperBlockObject(const Superblock& sb) : sb_(sb) {}

  // DiskObject interface:
  const char* GetName() const override { return kSuperBlockName; }

  uint32_t GetNumElements() const override { return kSuperblockNumElements; }

  void GetValue(const void** out_buffer, size_t* out_buffer_size) const override;

  std::unique_ptr<DiskObject> GetElementAt(uint32_t index) const override;

 private:
  // minfs superblock object.
  const Superblock sb_;
};

}  // namespace minfs

#endif  // ZIRCON_SYSTEM_ULIB_MINFS_INSPECTOR_SUPERBLOCK_H_
