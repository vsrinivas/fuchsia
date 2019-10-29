// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "superblock.h"

#include "root-object.h"

namespace blobfs {

void SuperblockObject::GetValue(const void** out_buffer, size_t* out_buffer_size) const {
  ZX_DEBUG_ASSERT_MSG(false, "Invalid GetValue call for non primitive data type.");
}
std::unique_ptr<disk_inspector::DiskObject> SuperblockObject::GetElementAt(uint32_t index) const {
  switch (index) {
    case 0:
      return CreateUint64DiskObj("magic0", &sb_.magic0);
    case 1:
      return CreateUint64DiskObj("magic1", &sb_.magic1);
    case 2:
      return CreateUint32DiskObj("version", &sb_.version);
    case 3:
      return CreateUint32DiskObj("flags", &sb_.flags);
    case 4:
      return CreateUint32DiskObj("block_size", &sb_.block_size);
    case 5:
      return CreateUint64DiskObj("data_block_count", &sb_.data_block_count);
    case 6:
      return CreateUint64DiskObj("journal_block_count", &sb_.journal_block_count);
    case 7:
      return CreateUint64DiskObj("inode_count", &sb_.inode_count);
    case 8:
      return CreateUint64DiskObj("alloc_block_count", &sb_.alloc_block_count);
    case 9:
      return CreateUint64DiskObj("alloc_inode_count", &sb_.alloc_inode_count);
    case 10:
      return CreateUint64DiskObj("unused", &sb_.blob_header_next);
    case 11:
      return CreateUint64DiskObj("slice_size", &sb_.slice_size);
    case 12:
      return CreateUint64DiskObj("vslice_count", &sb_.vslice_count);
    case 13:
      return CreateUint32DiskObj("abm_slices", &sb_.abm_slices);
    case 14:
      return CreateUint32DiskObj("ino_slices", &sb_.ino_slices);
    case 15:
      return CreateUint32DiskObj("dat_slices", &sb_.dat_slices);
    case 16:
      return CreateUint32DiskObj("journal_slices", &sb_.journal_slices);
  }
  return nullptr;
}

}  // namespace blobfs
