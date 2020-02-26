// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fs/journal/disk_struct.h"

#include <disk_inspector/type_utils.h>

namespace fs {

std::unique_ptr<disk_inspector::DiskStruct> GetJournalSuperblockStruct() {
  static_assert(sizeof(JournalInfo) == 40);
  std::unique_ptr<disk_inspector::DiskStruct> object =
      disk_inspector::DiskStruct::Create("Journal Superblock", sizeof(JournalInfo));
  ADD_FIELD(object, JournalInfo, magic);
  ADD_FIELD(object, JournalInfo, start_block);
  ADD_FIELD(object, JournalInfo, reserved);
  ADD_FIELD(object, JournalInfo, timestamp);
  ADD_FIELD(object, JournalInfo, checksum);
  return object;
}

std::unique_ptr<disk_inspector::DiskStruct> GetJournalPrefixStruct() {
  static_assert(offsetof(JournalPrefix, reserved) == 24);
  std::unique_ptr<disk_inspector::DiskStruct> object =
      disk_inspector::DiskStruct::Create("Journal Prefix", sizeof(JournalPrefix));
  ADD_FIELD(object, JournalPrefix, magic);
  ADD_FIELD(object, JournalPrefix, sequence_number);
  ADD_FIELD(object, JournalPrefix, flags);
  ADD_FIELD(object, JournalPrefix, reserved);
  return object;
}

std::unique_ptr<disk_inspector::DiskStruct> GetJournalHeaderBlockStruct(uint64_t index) {
  static_assert(offsetof(JournalHeaderBlock, reserved) == 8188);
  std::unique_ptr<disk_inspector::DiskStruct> object = disk_inspector::DiskStruct::Create(
      "Journal Header, Block #" + std::to_string(index), sizeof(JournalHeaderBlock));
  ADD_STRUCT_FIELD(object, JournalHeaderBlock, prefix, GetJournalPrefixStruct());
  ADD_FIELD(object, JournalHeaderBlock, payload_blocks);
  ADD_ARRAY_FIELD(object, JournalHeaderBlock, target_blocks, kMaxBlockDescriptors);
  ADD_ARRAY_FIELD(object, JournalHeaderBlock, target_flags, kMaxBlockDescriptors);
  ADD_FIELD(object, JournalHeaderBlock, reserved);
  return object;
}

std::unique_ptr<disk_inspector::DiskStruct> GetJournalCommitBlockStruct(uint64_t index) {
  static_assert(sizeof(JournalCommitBlock) == 40);
  std::unique_ptr<disk_inspector::DiskStruct> object = disk_inspector::DiskStruct::Create(
      "Journal Commit, Block #" + std::to_string(index), sizeof(JournalCommitBlock));
  ADD_STRUCT_FIELD(object, JournalCommitBlock, prefix, GetJournalPrefixStruct());
  ADD_FIELD(object, JournalCommitBlock, checksum);
  return object;
}

}  // namespace fs
