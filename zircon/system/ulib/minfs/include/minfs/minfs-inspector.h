// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINFS_MINFS_INSPECTOR_H_
#define MINFS_MINFS_INSPECTOR_H_

#include <string>
#include <variant>
#include <vector>

#include <block-client/cpp/block-device.h>
#include <disk_inspector/common_types.h>
#include <disk_inspector/inspector_transaction_handler.h>
#include <fs/journal/format.h>
#include <minfs/format.h>
#include <storage/buffer/vmo_buffer.h>

namespace minfs {

// Bare-bone minfs inspector that loads metadata from the backing block
// device and provides functions to return parsed structs.
class MinfsInspector {
 public:
  // Creates a MinfsInspector from a block device. Loads the fs metadata from
  // disk into buffers upon creation.
  static zx_status_t Create(std::unique_ptr<block_client::BlockDevice>,
                            std::unique_ptr<MinfsInspector>* out);

  // This function is used to initialize minfs metadata buffers and to load the relavent data.
  zx_status_t Initialize();

  // Returns the superblock from the buffer cache.
  Superblock InspectSuperblock();

  // Returns the Inode at |index| from the buffer cache.
  Inode InspectInode(uint64_t index);

  // Returns the number of inodes as defined in the cached superblock.
  uint64_t GetInodeCount();

  // Returns whether an inode is allocated from the cached metadata.
  bool CheckInodeAllocated(uint64_t index);

  // Returns the journal info from the cached metadata.
  fs::JournalInfo InspectJournalSuperblock();

  // Parses and returns the journal prefix from the cached journal entry block
  // at |index|. This may not necessarily be a valid prefix if the block index
  // does not represent a journal entry with a prefix.
  fs::JournalPrefix InspectJournalPrefix(uint64_t index);

  // Parses and returns the journal header block from the cached journal entry
  // block at |index|. This may not necessarily be a valid header if the block
  // index does not represent a journal header block.
  fs::JournalHeaderBlock InspectJournalHeader(uint64_t index);

  // Parses and returns the journal commit block from the cached journal entry
  // block at |index|. This may not necessarily be a valid header if the block
  // index does not represent a journal commit block.
  fs::JournalCommitBlock InspectJournalCommit(uint64_t index);

  // Loads and returns the backup superblock into the argument. Errors if the
  // load fails.
  zx_status_t InspectBackupSuperblock(Superblock* backup);

 private:
  explicit MinfsInspector(std::unique_ptr<disk_inspector::InspectorTransactionHandler> handler)
      : handler_(std::move(handler)) {}

  std::unique_ptr<disk_inspector::InspectorTransactionHandler> handler_;
  std::unique_ptr<storage::VmoBuffer> superblock_;
  std::unique_ptr<storage::VmoBuffer> inode_bitmap_;
  std::unique_ptr<storage::VmoBuffer> inode_table_;
  std::unique_ptr<storage::VmoBuffer> journal_;
};

}  // namespace minfs

#endif  // MINFS_MINFS_INSPECTOR_H_
