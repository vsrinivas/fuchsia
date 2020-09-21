// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "disk_struct.h"

#include <zircon/assert.h>

#include <disk_inspector/disk_struct.h>
#include <disk_inspector/type_utils.h>
#include <minfs/format.h>

namespace minfs {

std::unique_ptr<disk_inspector::DiskStruct> GetSuperblockStruct() {
  static_assert(offsetof(Superblock, reserved) == 120);
  std::unique_ptr<disk_inspector::DiskStruct> object =
      disk_inspector::DiskStruct::Create("Superblock", sizeof(Superblock));
  ADD_FIELD(object, Superblock, magic0);
  ADD_FIELD(object, Superblock, magic1);
  ADD_FIELD(object, Superblock, version_major);
  ADD_FIELD(object, Superblock, version_minor);
  ADD_FIELD(object, Superblock, checksum);
  ADD_FIELD(object, Superblock, generation_count);
  ADD_FIELD(object, Superblock, flags);
  ADD_FIELD(object, Superblock, block_size);
  ADD_FIELD(object, Superblock, inode_size);
  ADD_FIELD(object, Superblock, block_count);
  ADD_FIELD(object, Superblock, inode_count);
  ADD_FIELD(object, Superblock, alloc_block_count);
  ADD_FIELD(object, Superblock, alloc_inode_count);
  ADD_FIELD(object, Superblock, ibm_block);
  ADD_FIELD(object, Superblock, abm_block);
  ADD_FIELD(object, Superblock, ino_block);
  ADD_FIELD(object, Superblock, integrity_start_block);
  ADD_FIELD(object, Superblock, dat_block);
  ADD_FIELD(object, Superblock, slice_size);
  ADD_FIELD(object, Superblock, vslice_count);
  ADD_FIELD(object, Superblock, ibm_slices);
  ADD_FIELD(object, Superblock, abm_slices);
  ADD_FIELD(object, Superblock, ino_slices);
  ADD_FIELD(object, Superblock, integrity_slices);
  ADD_FIELD(object, Superblock, dat_slices);
  ADD_FIELD(object, Superblock, unlinked_head);
  ADD_FIELD(object, Superblock, unlinked_tail);
  ADD_FIELD(object, Superblock, oldest_revision);
  ADD_ARRAY_FIELD(object, Superblock, reserved, 2018);
  return object;
}

std::unique_ptr<disk_inspector::DiskStruct> GetInodeStruct(uint64_t index) {
  static_assert(sizeof(Inode) == 256);
  std::unique_ptr<disk_inspector::DiskStruct> object =
      disk_inspector::DiskStruct::Create("Inode " + std::to_string(index), sizeof(Inode));
  ADD_FIELD(object, Inode, magic);
  ADD_FIELD(object, Inode, size);
  ADD_FIELD(object, Inode, block_count);
  ADD_FIELD(object, Inode, link_count);
  ADD_FIELD(object, Inode, create_time);
  ADD_FIELD(object, Inode, modify_time);
  ADD_FIELD(object, Inode, seq_num);
  ADD_FIELD(object, Inode, gen_num);
  ADD_FIELD(object, Inode, dirent_count);
  ADD_FIELD(object, Inode, last_inode);
  ADD_FIELD(object, Inode, next_inode);
  ADD_ARRAY_FIELD(object, Inode, rsvd, 3);
  ADD_ARRAY_FIELD(object, Inode, dnum, kMinfsDirect);
  ADD_ARRAY_FIELD(object, Inode, inum, kMinfsIndirect);
  ADD_ARRAY_FIELD(object, Inode, dinum, kMinfsDoublyIndirect);
  return object;
}

}  // namespace minfs
