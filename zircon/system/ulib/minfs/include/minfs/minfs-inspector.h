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
// TODO(fxb/47359): Since this can run on corrupt data, more thought needs
// to be put on the potential edge cases that can happen during corruption.
// Care needs to be put into what dependencies are used when exposing new
// information from this class.
class MinfsInspector {
 public:
  // Creates a MinfsInspector from a block device. Tries to load the fs
  // metadata from disk into buffers upon creation by calling both
  // ReloadSuperblock() and ReloadMetadataFromSuperblock() in succession.
  static zx_status_t Create(std::unique_ptr<block_client::BlockDevice>,
                            std::unique_ptr<MinfsInspector>* out);

  // This function is used to initialize minfs metadata buffers and to load the relavent data.
  zx_status_t Initialize();

  // Initializes the |superblock_| buffer and tries to load the superblock
  // from disk into the buffer. The MinfsInspector should be considered invalid
  // and should not be used if this function fails as either VmoBuffers cannot
  // be created or we cannot read even the first block from the underlying
  // block device.
  zx_status_t ReloadSuperblock();

  // Initializes the |inode_bitmap_|, |inode_table_|, and |journal_| buffers
  // based on |superblock_| and tries to load the associated structs from disk
  // into these buffers. Note: we do not consider the failure of initializing
  // and loading of any of these buffers to be errors to crash the program as
  // the class should still work to a reasonable degree in the case of debugging
  // a superblock with corruptions. For cases of failure, these bufffers have
  // undefined size and data inside. It is up to users to make sure that they
  // make valid calls using other functions in this class.
  void ReloadMetadataFromSuperblock();

  // Returns the superblock from the buffer cache.
  Superblock InspectSuperblock();

  // Returns the number of inodes as calculated from the size of |inode_table_|
  // buffer.
  uint64_t GetInodeCount();

  // Returns the number of inode allocation bits as calcuated from the size
  // of |inode_bitmap_| buffer.
  uint64_t GetInodeBitmapCount();

  // Returns the Inode at |index| from the buffer cache. Applications should
  // check to make sure |index| is within range using GetInodeCount().
  Inode InspectInode(uint64_t index);

  // Returns whether an inode is allocated from the cached metadata. Applications
  // should check to make sure |index| is within range using GetInodeBitmapCount().
  bool CheckInodeAllocated(uint64_t index);

  // Returns the journal info from the cached metadata. If the |journal| buffer
  // is not initialized, returns a zeroed out JournalSuperblock.
  fs::JournalInfo InspectJournalSuperblock();

  // Returns the number of journal entires calculated from the size of |journal_|
  // buffer.
  uint64_t GetJournalEntryCount();

  // Parses and returns the journal prefix from the cached journal entry block
  // at |index|. This may not necessarily be a valid prefix if the block index
  // does not represent a journal entry with a prefix. Applications should
  // check to make sure |index| is within range using GetJournalEntryCount().
  fs::JournalPrefix InspectJournalPrefix(uint64_t index);

  // Parses and returns the journal header block from the cached journal entry
  // block at |index|. This may not necessarily be a valid header if the block
  // index does not represent a journal header block. Applications should
  // check to make sure |index| is within range using GetJournalEntryCount().
  fs::JournalHeaderBlock InspectJournalHeader(uint64_t index);

  // Parses and returns the journal commit block from the cached journal entry
  // block at |index|. This may not necessarily be a valid header if the block
  // index does not represent a journal commit block. Applications should
  // check to make sure |index| is within range using GetJournalEntryCount().
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
