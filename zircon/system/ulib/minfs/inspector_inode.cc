// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "inspector_inode.h"

#include <disk_inspector/common_types.h>

#include "inspector_private.h"

namespace minfs {

void InodeObject::GetValue(const void** out_buffer, size_t* out_buffer_size) const {
  ZX_DEBUG_ASSERT_MSG(false, "Invalid GetValue call for non primitive data type.");
}

std::unique_ptr<disk_inspector::DiskObject> InodeObject::GetElementAt(uint32_t index) const {
  switch (index) {
    case 0: {
      // uint32_t magic
      return CreateUint32DiskObj("magic", &(inode_.magic));
    }
    case 1: {
      // uint32_t size
      return CreateUint32DiskObj("size", &(inode_.size));
    }
    case 2: {
      // uint32_t block_count
      return CreateUint32DiskObj("block_count", &(inode_.block_count));
    }
    case 3: {
      // uint32_t link_count
      return CreateUint32DiskObj("link_count", &(inode_.link_count));
    }
    case 4: {
      // uint64_t create_time
      return CreateUint64DiskObj("create_time", &(inode_.create_time));
    }
    case 5: {
      // uint64_t modify_time
      return CreateUint64DiskObj("modify_time", &(inode_.modify_time));
    }
    case 6: {
      // uint32_t seq_num
      return CreateUint32DiskObj("seq_num", &(inode_.seq_num));
    }
    case 7: {
      // uint32_t gen_num
      return CreateUint32DiskObj("gen_num", &(inode_.gen_num));
    }
    case 8: {
      // uint32_t dirent_count
      return CreateUint32DiskObj("dirent_count", &(inode_.dirent_count));
    }
    case 9: {
      // ino_t/uint32_t last_inode
      return CreateUint32DiskObj("last_inode", &(inode_.last_inode));
    }
    case 10: {
      // ino_t/uint32_t next_inode
      return CreateUint32DiskObj("next_inode", &(inode_.next_inode));
    }
    case 11: {
      // uint32_t Array rsvd
      return CreateUint32ArrayDiskObj("reserved", inode_.rsvd, 3);
    }
    case 12: {
      // blk_t/uint32_t Array dnum
      return CreateUint32ArrayDiskObj("direct blocks", inode_.dnum, kMinfsDirect);
    }
    case 13: {
      // blk_t/uint32_t Array inum
      return CreateUint32ArrayDiskObj("indirect blocks", inode_.inum, kMinfsIndirect);
    }
    case 14: {
      // blk_t/uint32_t Array dinum
      return CreateUint32ArrayDiskObj("double indirect blocks", inode_.dinum, kMinfsDoublyIndirect);
    }
  }
  return nullptr;
}

}  // namespace minfs
