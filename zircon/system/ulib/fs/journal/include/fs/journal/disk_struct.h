// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_JOURNAL_DISK_STRUCT_H_
#define FS_JOURNAL_DISK_STRUCT_H_

#include <disk_inspector/disk_struct.h>
#include <fs/journal/format.h>

namespace fs {

// Free functions to get journal struct layouts from
// <fs/journal/format.h> into disk_inspector::DiskStructs to support parsing
// structs and their fields into strings and editing structs from
// string fields and values.

// Creates a DiskStruct object representing a JournalInfo struct.
std::unique_ptr<disk_inspector::DiskStruct> GetJournalSuperblockStruct();

// Creates a DiskStruct object representing a JournalPrefix struct.
std::unique_ptr<disk_inspector::DiskStruct> GetJournalPrefixStruct();

// Creates a DiskStruct object representing a JournalHeaderBlock struct.
// |index| represents the index of the journal entry block and is stored as
// part of the name of the DiskStruct.
std::unique_ptr<disk_inspector::DiskStruct> GetJournalHeaderBlockStruct(uint64_t index);

// Creates a DiskStruct object representing a JournalCommitBlock struct.
// |index| represents the index of the journal entry block and is stored as
// part of the name of the DiskStruct.
std::unique_ptr<disk_inspector::DiskStruct> GetJournalCommitBlockStruct(uint64_t index);

}  // namespace fs

#endif  // FS_JOURNAL_DISK_STRUCT_H_
