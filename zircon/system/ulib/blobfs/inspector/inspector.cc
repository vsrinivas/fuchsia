// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/inspector/inspector.h>

#include <lib/disk-inspector/common-types.h>

#include <block-client/cpp/block-device.h>

#include "root-object.h"
#include "superblock.h"

namespace blobfs {

std::unique_ptr<disk_inspector::DiskObjectUint64> CreateUint64DiskObj(fbl::String field_name,
                                                                      const uint64_t* value) {
  return std::make_unique<disk_inspector::DiskObjectUint64>(field_name, value);
}

std::unique_ptr<disk_inspector::DiskObjectUint32> CreateUint32DiskObj(fbl::String field_name,
                                                                      const uint32_t* value) {
  return std::make_unique<disk_inspector::DiskObjectUint32>(field_name, value);
}

zx_status_t Inspector::GetRoot(std::unique_ptr<disk_inspector::DiskObject>* out) {
  MountOptions options = {};
  options.writability = Writability::ReadOnlyDisk;
  options.journal = false;
  std::unique_ptr<Blobfs> fs;
  zx_status_t status = Blobfs::Create(std::move(device_), &options, &fs);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs Inspector: Create Failed to Create Blobfs: %d\n", status);
    return status;
  }
  out->reset(new RootObject(std::move(fs)));
  return ZX_OK;
}

void RootObject::GetValue(const void** out_buffer, size_t* out_buffer_size) const {
  ZX_DEBUG_ASSERT_MSG(false, "Invalid GetValue call for non primitive data type.");
}

std::unique_ptr<disk_inspector::DiskObject> RootObject::GetSuperblock() const {
  return std::unique_ptr<disk_inspector::DiskObject>(
      new SuperblockObject(inspector_blobfs_->GetSuperblock()));
}

std::unique_ptr<disk_inspector::DiskObject> RootObject::GetInodeTable() const { return nullptr; }

std::unique_ptr<disk_inspector::DiskObject> RootObject::GetJournal() const { return nullptr; }

std::unique_ptr<disk_inspector::DiskObject> RootObject::GetElementAt(uint32_t index) const {
  switch (index) {
    case 0: {
      // Super block
      return GetSuperblock();
    }
  }
  return nullptr;
}

}  // namespace blobfs
