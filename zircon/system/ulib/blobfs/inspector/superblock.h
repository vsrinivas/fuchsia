// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_BLOBFS_INSPECTOR_SUPERBLOCK_H_
#define ZIRCON_SYSTEM_ULIB_BLOBFS_INSPECTOR_SUPERBLOCK_H_

#include <lib/disk-inspector/common-types.h>

#include <memory>

#include <blobfs/format.h>
#include <fs/journal/format.h>

namespace blobfs {

// Total number of fields in the on-disk superblock structure.
constexpr uint32_t kSuperblockNumElements = 16;
constexpr char kSuperblockName[] = "superblock";

class SuperblockObject : public disk_inspector::DiskObject {
 public:
  SuperblockObject() = delete;
  SuperblockObject(const SuperblockObject&) = delete;
  SuperblockObject(SuperblockObject&&) = delete;
  SuperblockObject& operator=(const SuperblockObject&) = delete;
  SuperblockObject& operator=(SuperblockObject&&) = delete;

  explicit SuperblockObject(const Superblock& sb) : sb_(sb) {}

  // DiskObject interface:
  const char* GetName() const override { return kSuperblockName; }

  uint32_t GetNumElements() const override { return kSuperblockNumElements; }

  void GetValue(const void** out_buffer, size_t* out_buffer_size) const override;

  std::unique_ptr<DiskObject> GetElementAt(uint32_t index) const override;

 private:
  // Superblock object.
  const Superblock sb_;
};

}  // namespace blobfs

#endif  // ZIRCON_SYSTEM_ULIB_BLOBFS_INSPECTOR_SUPERBLOCK_H_
