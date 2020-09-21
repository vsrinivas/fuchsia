// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MINFS_INSPECTOR_DISK_STRUCT_H_
#define SRC_STORAGE_MINFS_INSPECTOR_DISK_STRUCT_H_

#include <disk_inspector/disk_struct.h>

namespace minfs {

// Free functions to get minfs struct layouts defined in <minfs/format.h>
// into disk_inspector::DiskStructs to support parsing of structs
// and their fields into strings and editing structs from
// string fields and values.

// Creates a DiskStruct object representing a Superblock struct.
std::unique_ptr<disk_inspector::DiskStruct> GetSuperblockStruct();

// Creates a DiskStruct object representing an Inode struct.
// |index| represents the index of the inode and is stored as part of the
// name of the DiskStruct.
std::unique_ptr<disk_inspector::DiskStruct> GetInodeStruct(uint64_t index);

}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_INSPECTOR_DISK_STRUCT_H_
