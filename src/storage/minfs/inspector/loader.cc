// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "loader.h"

#include <zircon/assert.h>

namespace minfs {

zx_status_t Loader::LoadSuperblock(uint64_t dev_offset, storage::BlockBuffer* buffer) const {
  return RunReadOperation(buffer, 0, dev_offset, 1);
}

zx_status_t Loader::LoadInodeBitmap(const Superblock& superblock,
                                    storage::BlockBuffer* buffer) const {
  return RunReadOperation(buffer, 0, superblock.ibm_block, InodeBitmapBlocks(superblock));
}

zx_status_t Loader::LoadInodeTable(const Superblock& superblock,
                                   storage::BlockBuffer* buffer) const {
  return RunReadOperation(buffer, 0, superblock.ino_block, InodeBlocks(superblock));
}

zx_status_t Loader::LoadJournal(const Superblock& superblock, storage::BlockBuffer* buffer) const {
  return RunReadOperation(buffer, 0, JournalStartBlock(superblock), JournalBlocks(superblock));
}

zx_status_t Loader::RunReadOperation(storage::BlockBuffer* buffer, uint64_t vmo_offset,
                                     uint64_t dev_offset, uint64_t length) const {
  ZX_ASSERT(buffer->capacity() - vmo_offset >= length);
  storage::Operation operation{
      .type = storage::OperationType::kRead,
      .vmo_offset = vmo_offset,
      .dev_offset = dev_offset,
      .length = length,
  };
  return handler_->RunOperation(operation, buffer);
}

zx_status_t Loader::RunWriteOperation(storage::BlockBuffer* buffer, uint64_t vmo_offset,
                                      uint64_t dev_offset, uint64_t length) const {
  ZX_ASSERT(buffer->capacity() - vmo_offset >= length);
  storage::Operation operation{
      .type = storage::OperationType::kWrite,
      .vmo_offset = vmo_offset,
      .dev_offset = dev_offset,
      .length = length,
  };
  return handler_->RunOperation(operation, buffer);
}

}  // namespace minfs
