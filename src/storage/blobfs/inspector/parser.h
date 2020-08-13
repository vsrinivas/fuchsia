// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_INSPECTOR_PARSER_H_
#define SRC_STORAGE_BLOBFS_INSPECTOR_PARSER_H_

#include <blobfs/format.h>
#include <storage/buffer/block_buffer.h>

namespace blobfs {

// Non-member functions to interpret BlockBuffer contents as on-disk structs.
// These functions take a block buffer that is assumed to have the relevant
// data already loaded.

// Parses the first block in the argument buffer as a Superblock.
Superblock GetSuperblock(storage::BlockBuffer* buffer);

// Parses the bit at specified index in the buffer following
// the ulib/bitmap implementation, differing in that this function
// uses uint64_t unlike size_t for the bitmap implementation. Assumes
// the data in the entire buffer is part of the bitmap.
bool GetBitmapElement(storage::BlockBuffer* buffer, uint64_t index);

// Parses the inode at the specified index in the buffer following
// on-disk format. Assumes the data in the entire buffer is the
// inode table. No dedicated GetExtentContainer function is provided.
// The inode's asExtentContainer function should be used to provide that
// functionality.
Inode GetInodeElement(storage::BlockBuffer* buffer, uint64_t index);

// Writes the bit at specified index in the buffer following
// the ulib/bitmap implementation, differing in that this function
// uses uint64_t unlike size_t for the bitmap implementation. Assumes
// the data in the entire buffer is part of the bitmap.
void WriteBitmapElement(storage::BlockBuffer* buffer, bool value, uint64_t index);

// Writes the inode at the specified index in the buffer following
// on-disk format. Assumes the data in the entire buffer is the
// inode table.
void WriteInodeElement(storage::BlockBuffer* buffer, Inode inode, uint64_t index);

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_INSPECTOR_PARSER_H_
