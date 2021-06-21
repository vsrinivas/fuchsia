// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/inspector/disk_struct.h"

#include <iostream>

#include <gtest/gtest.h>

#include "src/storage/minfs/format.h"

namespace minfs {
namespace {

// These tests are just to make sure the returned string has all the required
// fields.

TEST(InspectorDiskStruct, GetSuperblockString) {
  std::unique_ptr<disk_inspector::DiskStruct> disk_struct = GetSuperblockStruct();
  Superblock sb = {};
  disk_inspector::PrintOptions options;
  options.display_hex = false;
  options.hide_array = true;

  std::string output = R"""(Name: Superblock
	magic0: 0
	magic1: 0
	major_version: 0
	checksum: 0
	generation_count: 0
	flags: 0
	block_size: 0
	inode_size: 0
	block_count: 0
	inode_count: 0
	alloc_block_count: 0
	alloc_inode_count: 0
	ibm_block: 0
	abm_block: 0
	ino_block: 0
	integrity_start_block: 0
	dat_block: 0
	slice_size: 0
	ibm_slices: 0
	abm_slices: 0
	ino_slices: 0
	integrity_slices: 0
	dat_slices: 0
	unlinked_head: 0
	unlinked_tail: 0
	oldest_minor_version: 0
	reserved: uint32_t[2018] = { ... }
)""";

  EXPECT_EQ(disk_struct->ToString(&sb, options), output);
}

TEST(InspectorDiskStruct, GetInodeString) {
  std::unique_ptr<disk_inspector::DiskStruct> disk_struct = GetInodeStruct(0);
  Inode inode = {};
  disk_inspector::PrintOptions options;
  options.display_hex = false;
  options.hide_array = true;

  std::string output = R"""(Name: Inode 0
	magic: 0
	size: 0
	block_count: 0
	link_count: 0
	create_time: 0
	modify_time: 0
	seq_num: 0
	gen_num: 0
	dirent_count: 0
	last_inode: 0
	next_inode: 0
	rsvd: uint32_t[3] = { ... }
	dnum: uint32_t[16] = { ... }
	inum: uint32_t[31] = { ... }
	dinum: uint32_t[1] = { ... }
)""";
  EXPECT_EQ(disk_struct->ToString(&inode, options), output);
}

}  // namespace
}  // namespace minfs
