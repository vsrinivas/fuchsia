// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains functions that are shared between host
// and target implementations of .

#ifndef SRC_STORAGE_FACTORY_FACTORYFS_SUPERBLOCK_H_
#define SRC_STORAGE_FACTORY_FACTORYFS_SUPERBLOCK_H_

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <zircon/types.h>

#include <fbl/macros.h>
#include <fbl/string_buffer.h>

#include "src/storage/factory/factoryfs/format.h"

namespace factoryfs {

// Validates the metadata of a factoryfs superblock.
zx_status_t CheckSuperblock(const Superblock* info);

// Creates a superblock, formatted for |block_count| disk blocks.
void InitializeSuperblock(uint64_t block_count, Superblock* info);

}  // namespace factoryfs

#endif  // SRC_STORAGE_FACTORY_FACTORYFS_SUPERBLOCK_H_
