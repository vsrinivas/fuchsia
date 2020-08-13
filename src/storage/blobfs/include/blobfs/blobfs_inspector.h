// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_INCLUDE_BLOBFS_BLOBFS_INSPECTOR_H_
#define SRC_STORAGE_BLOBFS_INCLUDE_BLOBFS_BLOBFS_INSPECTOR_H_

#include <lib/zx/status.h>

#include <string>
#include <vector>

#include <blobfs/format.h>
#include <disk_inspector/buffer_factory.h>
#include <disk_inspector/common_types.h>
#include <disk_inspector/loader.h>
#include <fs/journal/format.h>
#include <fs/transaction/transaction_handler.h>

namespace blobfs {

// Bare-bone blobfs inspector that loads metadata from the backing block device and provides
// functions to return parsed structs.
class BlobfsInspector {
 public:
  // Creates a MinfsInspector from a block device. Tries to load the superblock from disk upon
  // creation by calling ReloadSuperblock().
  static zx::status<std::unique_ptr<BlobfsInspector>> Create(
      std::unique_ptr<fs::TransactionHandler> handler,
      std::unique_ptr<disk_inspector::BufferFactory> factory);

  // For functions which allow passing in a block buffer to either read/write data, users need to
  // make sure that the buffer is compatible with the inspector's |handler_|. We expose the buffer
  // factory here for users to grab buffers that work with the handler if they cannot create
  // compatible buffers themselves.
  const disk_inspector::BufferFactory* GetBufferFactory() const { return buffer_factory_.get(); }

  // This function is used to initialize minfs metadata buffers and to load the relavent data.
  zx_status_t Initialize();

  // Initializes the |superblock_| buffer and tries to load the superblock from disk into the
  // buffer. The MinfsInspector should be considered invalid and should not be used if this function
  // fails as either VmoBuffers cannot be created or we cannot read even the first block from the
  // underlying block device.
  zx_status_t ReloadSuperblock();

  // Initializes the |inode_bitmap_|, |inode_table_|, and |journal_| buffers based on |superblock_|
  // and tries to load the associated structs from disk into these buffers. Note: we do not consider
  // the failure of initializing and loading of any of these buffers to be errors to crash the
  // program as the class should still work to a reasonable degree in the case of debugging a
  // superblock with corruptions. For cases of failure, these bufffers have undefined size and data
  // inside. It is up to users to make sure that they make valid calls using other functions in this
  // class.
  void ReloadMetadataFromSuperblock();

  // Returns a copy of |superblock_|.
  Superblock InspectSuperblock();

  // Returns the number of inodes from |superblock_|.
  uint64_t GetInodeCount();

  // Returns the number of journal entires calculated from |superblock_|.
  uint64_t GetJournalEntryCount();

  // The following functions need to load data from disk, leading to the possibility of failed
  // loads. Since they need to return values, we have fit::results for all of the return types. In
  // addition, they all depend on the loaded |superblock_| value to get where to start indexing.

  // Loads the inode table blocks for which the inodes from |start_index| inclusive to |end_index|
  // exclusive from disk and returns the Inodes in the range as a vector.
  zx::status<std::vector<Inode>> InspectInodeRange(uint64_t start_index, uint64_t end_index);

  // Loads the first journal block
  zx::status<fs::JournalInfo> InspectJournalSuperblock();

  // Loads the |index| element journal entry block and returns it as a struct of type T. Only
  // supports casting to fs::JournalPrefix, fs::JournalHeaderBlock, and fs::JournalCommitBlock.
  template <typename T>
  zx::status<T> InspectJournalEntryAs(uint64_t index);

  // Loads the data bitmap blocks where the allocation bits for data blocks from |start_index|
  // inclusive to |end_index| exclusive are located from disk and returns the indices for which the
  // corresponding bits are allocated.
  zx::status<std::vector<uint64_t>> InspectDataBlockAllocatedInRange(uint64_t start_index,
                                                                     uint64_t end_index);

  // Writes the |superblock| argument to disk and sets |superblock_| to |superblock| if the write
  // succeeds.
  zx::status<> WriteSuperblock(Superblock superblock);

  // Writes the vector of |inodes| to disk as the range of inodes starting at |start_index| based on
  // |superblock_| specified location. If users wish to write ExtentContainers, we expect them to
  // treat the containers as Inodes and use this function to write them.
  zx::status<> WriteInodes(std::vector<Inode> inodes, uint64_t start_index);

  // Writes the |journal_info| as the journal superblock based on |superblock_| specified location.
  zx::status<> WriteJournalSuperblock(fs::JournalInfo journal_info);

  // Treats the entire |buffer| as journal entry blocks and writes the entire buffer to disk
  // starting at the |start_index| journal entry block.
  zx::status<> WriteJournalEntryBlocks(storage::BlockBuffer* buffer, uint64_t start_index);

  // Sets the block allocation bitmap bits starting from |start_index| inclusive to |end_index|
  // exclusive to |value| based on |superblock_| specified location.
  zx::status<> WriteDataBlockAllocationBits(bool value, uint64_t start_index, uint64_t end_index);

  // Writes the entire |buffer| to the data segment of disk starting at |start_index| data block
  // based on |superblock_| specified location.
  zx::status<> WriteDataBlocks(storage::BlockBuffer* buffer, uint64_t start_index);

 private:
  explicit BlobfsInspector(std::unique_ptr<fs::TransactionHandler> handler,
                           std::unique_ptr<disk_inspector::BufferFactory> buffer_factory);

  zx_status_t LoadNodeElement(storage::BlockBuffer* buffer, uint64_t index);
  zx_status_t LoadJournalEntry(storage::BlockBuffer* buffer, uint64_t index);

  std::unique_ptr<fs::TransactionHandler> handler_;
  std::unique_ptr<disk_inspector::BufferFactory> buffer_factory_;
  disk_inspector::Loader loader_;
  Superblock superblock_ = {};
  // Scratch buffer initialized to be a single block in the Create method.  Functions that use this
  // buffer should try to treat it as an initialized buffer only valid for the duration of the
  // function without any presaved state or ability for the function to save state.
  std::unique_ptr<storage::BlockBuffer> buffer_;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_INCLUDE_BLOBFS_BLOBFS_INSPECTOR_H_
