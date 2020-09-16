// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fs/journal/disk_struct.h"

#include <iostream>

#include <fs/journal/format.h>
#include <gtest/gtest.h>

namespace fs {
namespace {

// These tests are just to make sure the returned string has all the required
// fields.

TEST(InspectorDiskStruct, GetJournalSuperblockString) {
  std::unique_ptr<disk_inspector::DiskStruct> disk_struct = GetJournalSuperblockStruct();
  JournalInfo info = {};
  disk_inspector::PrintOptions options;
  options.display_hex = false;
  options.hide_array = true;

  std::string output = R"""(Name: Journal Superblock
	magic: 0
	start_block: 0
	reserved: 0
	timestamp: 0
	checksum: 0
)""";

  EXPECT_EQ(disk_struct->ToString(&info, options), output);
}

TEST(InspectorDiskStruct, GetJournalPrefixString) {
  std::unique_ptr<disk_inspector::DiskStruct> disk_struct = GetJournalPrefixStruct();
  JournalPrefix info = {};
  disk_inspector::PrintOptions options;
  options.display_hex = false;
  options.hide_array = true;

  std::string output = R"""(Name: Journal Prefix
	magic: 0
	sequence_number: 0
	flags: 0
	reserved: 0
)""";

  EXPECT_EQ(disk_struct->ToString(&info, options), output);
}

TEST(InspectorDiskStruct, GetJournalHeaderBlockString) {
  std::unique_ptr<disk_inspector::DiskStruct> disk_struct = GetJournalHeaderBlockStruct(0);
  JournalHeaderBlock info = {};
  disk_inspector::PrintOptions options;
  options.display_hex = false;
  options.hide_array = true;

  std::string output = R"""(Name: Journal Header, Block #0
	prefix: Name: Journal Prefix
		magic: 0
		sequence_number: 0
		flags: 0
		reserved: 0
	
	payload_blocks: 0
	target_blocks: uint64_t[679] = { ... }
	target_flags: uint32_t[679] = { ... }
	reserved: 0
)""";

  EXPECT_EQ(disk_struct->ToString(&info, options), output);
}

TEST(InspectorDiskStruct, GetJournalCommitBlockString) {
  std::unique_ptr<disk_inspector::DiskStruct> disk_struct = GetJournalCommitBlockStruct(0);
  JournalCommitBlock info = {};
  disk_inspector::PrintOptions options;
  options.display_hex = false;
  options.hide_array = true;

  std::string output = R"""(Name: Journal Commit, Block #0
	prefix: Name: Journal Prefix
		magic: 0
		sequence_number: 0
		flags: 0
		reserved: 0
	
	checksum: 0
)""";

  EXPECT_EQ(disk_struct->ToString(&info, options), output);
}

}  // namespace
}  // namespace fs
