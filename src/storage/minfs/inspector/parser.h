// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MINFS_INSPECTOR_PARSER_H_
#define SRC_STORAGE_MINFS_INSPECTOR_PARSER_H_

#include <storage/buffer/block_buffer.h>

#include "src/storage/minfs/format.h"

namespace minfs {

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
// inode table.
Inode GetInodeElement(storage::BlockBuffer* buffer, uint64_t index);

}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_INSPECTOR_PARSER_H_
