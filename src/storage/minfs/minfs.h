// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MINFS_MINFS_H_
#define SRC_STORAGE_MINFS_MINFS_H_

#include <inttypes.h>
#include <lib/fit/function.h>
#include <lib/zx/status.h>

#include <functional>
#include <memory>
#include <utility>

#include "src/storage/minfs/bcache.h"
#include "src/storage/minfs/format.h"
#include "src/storage/minfs/mount.h"

namespace minfs {

// Controls the validation-checking performed by minfs when loading
// structures from disk.
enum class IntegrityCheck {
  // Do not attempt to validate structures on load. This is useful
  // for inspection tools, which do not depend on the correctness
  // of on-disk structures.
  kNone,
  // Validate structures (locally) before usage. This is the
  // recommended option for mounted filesystems.
  kAll,
};

// Indicates whether to update backup superblock.
enum class UpdateBackupSuperblock {
  // Do not write the backup superblock.
  kNoUpdate,
  // Update the backup superblock.
  kUpdate,
};

uint32_t CalculateVsliceCount(const Superblock& superblock);

// Format the partition backed by |bc| as MinFS.
zx::status<> Mkfs(const MountOptions& options, Bcache* bc);

// Format the partition backed by |bc| as MinFS.
inline zx::status<> Mkfs(Bcache* bc) { return Mkfs({}, bc); }

}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_MINFS_H_
