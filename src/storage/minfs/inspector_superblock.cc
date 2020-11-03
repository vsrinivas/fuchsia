// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/inspector_superblock.h"

#include <disk_inspector/common_types.h>

#include "src/storage/minfs/inspector_private.h"

namespace minfs {

void SuperBlockObject::GetValue(const void** out_buffer, size_t* out_buffer_size) const {
  ZX_DEBUG_ASSERT_MSG(false, "Invalid GetValue call for non primitive data type.");
}

std::unique_ptr<disk_inspector::DiskObject> SuperBlockObject::GetElementAt(uint32_t index) const {
  switch (index) {
    case 0: {
      // uint64_t magic0.
      return CreateUint64DiskObj("magic0", &(sb_.magic0));
    }
    case 1: {
      // uint64_t magic1.
      return CreateUint64DiskObj("magic1", &(sb_.magic1));
    }
    case 2: {
      // uint32_t format_version.
      return CreateUint32DiskObj("format_version", &(sb_.format_version));
    }
    case 3: {
      // uint32_t flags.
      return CreateUint32DiskObj("flags", &(sb_.flags));
    }
    case 4: {
      // uint32_t block_size.
      return CreateUint32DiskObj("block_size", &(sb_.block_size));
    }
    case 5: {
      // uint32_t inode_size.
      return CreateUint32DiskObj("inode_size", &(sb_.inode_size));
    }
    case 6: {
      // uint32_t block_count.
      return CreateUint32DiskObj("block_count", &(sb_.block_count));
    }
    case 7: {
      // uint32_t inode_count.
      return CreateUint32DiskObj("inode_count", &(sb_.inode_count));
    }
    case 8: {
      // uint32_t alloc_block_count.
      return CreateUint32DiskObj("alloc_block_count", &(sb_.alloc_block_count));
    }
    case 9: {
      // uint32_t alloc_inode_count.
      return CreateUint32DiskObj("alloc_inode_count", &(sb_.alloc_inode_count));
    }
    case 10: {
      // uint32_t/blk_t ibm_block.
      return CreateUint32DiskObj("ibm_block", &(sb_.ibm_block));
    }
    case 11: {
      // uint32_t/blk_t abm_block.
      return CreateUint32DiskObj("abm_block", &(sb_.abm_block));
    }
    case 12: {
      // uint32_t/blk_t ino_block.
      return CreateUint32DiskObj("ino_block", &(sb_.ino_block));
    }
    case 13: {
      // uint32_t/blk_t integrity_start_block.
      return CreateUint32DiskObj("integrity_start_block", &(sb_.integrity_start_block));
    }
    case 14: {
      // uint32_t/blk_t dat_block.
      return CreateUint32DiskObj("dat_block", &(sb_.dat_block));
    }
    case 15: {
      // uint32_t slice_size.
      return CreateUint32DiskObj("slice_size", &(sb_.slice_size));
    }
    case 16: {
      // uint32_t ibm_slices.
      return CreateUint32DiskObj("ibm_slices", &(sb_.ibm_slices));
    }
    case 17: {
      // uint32_t abm_slices.
      return CreateUint32DiskObj("abm_slices", &(sb_.abm_slices));
    }
    case 18: {
      // uint32_t ino_slices.
      return CreateUint32DiskObj("ino_slices", &(sb_.ino_slices));
    }
    case 19: {
      // uint32_t integrity_slices.
      return CreateUint32DiskObj("integrity_slices", &(sb_.integrity_slices));
    }
    case 20: {
      // uint32_t dat_slices.
      return CreateUint32DiskObj("dat_slices", &(sb_.dat_slices));
    }
    case 21: {
      // uint32_t/ino_t unlinked_head.
      return CreateUint32DiskObj("unlinked_head", &(sb_.unlinked_head));
    }
    case 22: {
      // uint32_t/ino_t unlinked_tail.
      return CreateUint32DiskObj("unlinked_tail", &(sb_.unlinked_tail));
    }
    case 23: {
      // uint32_t oldest_revision
      return CreateUint32DiskObj("oldest_revision", &(sb_.oldest_revision));
    }
    case 24: {
      //  uint32_t checksum.
      return CreateUint32DiskObj("checksum", &(sb_.checksum));
    }
    case 25: {
      //  uint32_t generation_count.
      return CreateUint32DiskObj("generation_count", &(sb_.generation_count));
    }
    case 26: {
      // uint32_t reserved[].
      return CreateUint32ArrayDiskObj("reserved", sb_.reserved, 1);
    }
  }
  return nullptr;
}

}  // namespace minfs
