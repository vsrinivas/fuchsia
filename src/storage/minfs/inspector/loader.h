// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MINFS_INSPECTOR_LOADER_H_
#define SRC_STORAGE_MINFS_INSPECTOR_LOADER_H_

#include <fs/transaction/transaction_handler.h>
#include <minfs/format.h>
#include <storage/buffer/block_buffer.h>

namespace minfs {

// Wrapper arround fs::TransactionHandler to load on-disk structures from a block-device
// into a passed-in BlockBuffer. Loading functions return an error status if the passed
// in buffer to load into is not large enough to fit the loaded data.
class Loader {
 public:
  explicit Loader(fs::TransactionHandler* handler) : handler_(handler) {}

  // Loads the superblock at the device offset from the block device backing the
  // handler to the start of the buffer.
  zx_status_t LoadSuperblock(uint64_t dev_offset, storage::BlockBuffer* buffer) const;

  // Loads the inode bitmap at the location specified by the superblock to the start of the buffer.
  zx_status_t LoadInodeBitmap(const Superblock& superblock, storage::BlockBuffer* buffer) const;

  // Loads the inode table at the location specified by the superblock to the start of the buffer.
  zx_status_t LoadInodeTable(const Superblock& superblock, storage::BlockBuffer* buffer) const;

  // Loads the journal at the location specified by the superblock to the start of the buffer.
  zx_status_t LoadJournal(const Superblock& superblock, storage::BlockBuffer* buffer) const;

  // Wrapper to send a read operation into |buffer| at the specified locations to the underlying
  // TransactionHandler.
  zx_status_t RunReadOperation(storage::BlockBuffer* buffer, uint64_t vmo_offset,
                               uint64_t dev_offset, uint64_t length) const;

  // Wrapper to send a write operation from |buffer| at the specified loations to the underlying
  // TransactionHandler.
  zx_status_t RunWriteOperation(storage::BlockBuffer* buffer, uint64_t vmo_offset,
                                uint64_t dev_offset, uint64_t length) const;

 private:
  fs::TransactionHandler* handler_;
};
}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_INSPECTOR_LOADER_H_
