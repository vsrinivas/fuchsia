// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/cksum.h>
#include <lib/syslog/cpp/macros.h>
#include <stdlib.h>
#include <string.h>

#include <memory>
#include <utility>

#include <bitmap/raw-bitmap.h>
#include <storage/buffer/block_buffer.h>

#include "src/lib/storage/vfs/cpp/transaction/buffered_operations_builder.h"
#include "src/storage/minfs/superblock.h"

namespace minfs {

SuperblockManager::SuperblockManager(const Superblock& info) {
  memcpy(info_blk_, &info, sizeof(Superblock));
}

SuperblockManager::~SuperblockManager() = default;

// static
zx::status<std::unique_ptr<SuperblockManager>> SuperblockManager::Create(const Superblock& info,
                                                                         uint32_t max_blocks,
                                                                         IntegrityCheck checks) {
  if (checks == IntegrityCheck::kAll) {
    if (auto status = CheckSuperblock(info, max_blocks); status.is_error()) {
      FX_LOGS(ERROR) << "SuperblockManager::Create failed to check info: " << status.error_value();
      return status.take_error();
    }
  }

  auto sb = std::unique_ptr<SuperblockManager>(new SuperblockManager(info));
  return zx::ok(std::move(sb));
}

void SuperblockManager::Write(PendingWork* transaction, UpdateBackupSuperblock write_backup) {
  UpdateChecksum(MutableInfo());
  fs::internal::BorrowedBuffer data(info_blk_);

  storage::Operation operation = {
      .type = storage::OperationType::kWrite,
      .vmo_offset = 0,
      .dev_offset = kSuperblockStart,
      .length = 1,
  };
  transaction->EnqueueMetadata(operation, &data);

  if (write_backup == UpdateBackupSuperblock::kUpdate) {
    blk_t superblock_dev_offset = kNonFvmSuperblockBackup;

    if (MutableInfo()->flags & kMinfsFlagFVM) {
      superblock_dev_offset = kFvmSuperblockBackup;
    }

    storage::Operation operation = {
        .type = storage::OperationType::kWrite,
        .vmo_offset = 0,
        .dev_offset = superblock_dev_offset,
        .length = 1,
    };
    transaction->EnqueueMetadata(operation, &data);
  }

  dirty_ = false;
}

}  // namespace minfs
