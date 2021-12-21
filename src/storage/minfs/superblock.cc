// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/superblock.h"

#include <lib/cksum.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/syslog/cpp/macros.h>
#include <stdlib.h>
#include <string.h>

#include <memory>
#include <utility>

#include <bitmap/raw-bitmap.h>

#include "src/storage/minfs/unowned_vmo_buffer.h"

namespace minfs {

SuperblockManager::SuperblockManager(const Superblock& info, fzl::OwnedVmoMapper mapper)
    : mapping_(std::move(mapper)) {}

SuperblockManager::~SuperblockManager() = default;

// static
zx::status<std::unique_ptr<SuperblockManager>> SuperblockManager::Create(
    block_client::BlockDevice* device, const Superblock& info, uint32_t max_blocks,
    IntegrityCheck checks) {
  if (checks == IntegrityCheck::kAll) {
    if (auto status = CheckSuperblock(info, device, max_blocks); status.is_error()) {
      FX_LOGS(ERROR) << "SuperblockManager::Create failed to check info: " << status.error_value();
      return status.take_error();
    }
  }

  fzl::OwnedVmoMapper mapper;
  // Create the info vmo
  if (auto status = mapper.CreateAndMap(kMinfsBlockSize, "minfs-superblock"); status != ZX_OK) {
    return zx::error(status);
  }

  memcpy(mapper.start(), &info, sizeof(Superblock));

  auto sb = std::unique_ptr<SuperblockManager>(new SuperblockManager(info, std::move(mapper)));
  return zx::ok(std::move(sb));
}

void SuperblockManager::Write(PendingWork* transaction, UpdateBackupSuperblock write_backup) {
  UpdateChecksum(MutableInfo());

  storage::Operation operation = {
      .type = storage::OperationType::kWrite,
      .vmo_offset = 0,
      .dev_offset = kSuperblockStart,
      .length = 1,
  };
  UnownedVmoBuffer buffer(zx::unowned_vmo(mapping_.vmo()));
  transaction->EnqueueMetadata(operation, &buffer);

  if (write_backup == UpdateBackupSuperblock::kUpdate) {
    blk_t superblock_dev_offset = kNonFvmSuperblockBackup;

    if (MutableInfo()->flags & kMinfsFlagFVM) {
      superblock_dev_offset = kFvmSuperblockBackup;
    }

    operation.dev_offset = superblock_dev_offset, transaction->EnqueueMetadata(operation, &buffer);
  }

  dirty_ = false;
}

}  // namespace minfs
