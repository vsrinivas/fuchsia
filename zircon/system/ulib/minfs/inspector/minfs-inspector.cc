// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minfs/minfs-inspector.h"

#include <algorithm>

#include <disk_inspector/inspector_transaction_handler.h>
#include <fs/journal/internal/inspector_parser.h>
#include <fs/trace.h>
#include <minfs/format.h>
#include <storage/buffer/vmo_buffer.h>

#include "loader.h"
#include "parser.h"

namespace minfs {

zx_status_t MinfsInspector::Create(std::unique_ptr<block_client::BlockDevice> device,
                                   std::unique_ptr<MinfsInspector>* out) {
  std::unique_ptr<disk_inspector::InspectorTransactionHandler> handler;
  zx_status_t status = disk_inspector::InspectorTransactionHandler::Create(
      std::move(device), kMinfsBlockSize, &handler);
  if (status != ZX_OK) {
    return status;
  }
  auto* inspector = new MinfsInspector(std::move(handler));
  status = inspector->buffer_.Initialize(inspector->handler_.get(), 1, kMinfsBlockSize,
                                         "scratch-buffer");
  if (status != ZX_OK) {
    return status;
  }
  status = inspector->ReloadSuperblock();
  if (status != ZX_OK) {
    return status;
  }
  out->reset(inspector);
  return ZX_OK;
}

zx_status_t MinfsInspector::ReloadSuperblock() {
  Loader loader(handler_.get());
  storage::VmoBuffer superblock;
  zx_status_t status = ZX_OK;
  status = superblock.Initialize(handler_.get(), kSuperblockBlocks, kMinfsBlockSize,
                                 "superblock-buffer");
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Cannot create superblock buffer. err: %d\n", status);
    return status;
  }
  status = loader.LoadSuperblock(kSuperblockStart, &superblock);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Cannot load superblock. err: %d\n", status);
    return status;
  }
  superblock_ = GetSuperblock(&superblock);
  return ZX_OK;
}

Superblock MinfsInspector::InspectSuperblock() { return superblock_; }

uint64_t MinfsInspector::GetInodeCount() { return superblock_.inode_count; }

uint64_t MinfsInspector::GetJournalEntryCount() {
  uint64_t journal_block_count = JournalBlocks(superblock_);
  // If there are no journal blocks, there cannot be any entries.
  if (journal_block_count == 0) {
    return 0;
  }
  return journal_block_count - fs::kJournalMetadataBlocks;
}

fit::result<std::vector<Inode>, zx_status_t> MinfsInspector::InspectInodeRange(uint64_t start_index,
                                                                               uint64_t end_index) {
  ZX_ASSERT(end_index > start_index);
  Loader loader(handler_.get());
  storage::VmoBuffer inode_buffer;

  // Since there are multiple inodes in a block, we first perform calculations
  // to find the block range of only the desired inode range to load.
  uint64_t start_block_offset = start_index / kMinfsInodesPerBlock;
  uint64_t start_block = superblock_.ino_block + start_block_offset;
  // Because the end index is exclusive, we calculate the length based on
  // end index - 1 to get the last inclusive value, and add 1 to the length
  // to prevent off-by-one.
  uint64_t block_length = (end_index - 1) / kMinfsInodesPerBlock - start_block_offset + 1;
  zx_status_t status;
  status = inode_buffer.Initialize(handler_.get(), block_length, kMinfsBlockSize, "inode-buffer");
  if (status != ZX_OK) {
    return fit::error(status);
  }
  status = loader.RunReadOperation(&inode_buffer, 0, start_block, block_length);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Cannot load inode. err: %d\n", status);
    return fit::error(status);
  }

  // Once loaded, we treat the buffer as the entire inode table and find the
  // new start index relative to it being in the first block. The element count
  // can be calculated normally.
  uint64_t buffer_offset = start_index % kMinfsInodesPerBlock;
  uint64_t count = end_index - start_index;
  std::vector<Inode> inodes;
  for (uint64_t i = 0; i < count; ++i) {
    inodes.emplace_back(GetInodeElement(&inode_buffer, buffer_offset + i));
  }
  return fit::ok(inodes);
}

fit::result<std::vector<uint64_t>, zx_status_t> MinfsInspector::InspectInodeAllocatedInRange(
    uint64_t start_index, uint64_t end_index) {
  ZX_ASSERT(end_index > start_index);
  Loader loader(handler_.get());
  storage::VmoBuffer bit_buffer;

  // Since there are multiple bits in a block, we first perform calculations
  // to find the block range of only the desired bit range to load.
  uint64_t start_block_offset = start_index / kMinfsBlockBits;
  uint64_t start_block = superblock_.ibm_block + start_block_offset;
  // Because the end index is exclusive, we calculate the length based on
  // end index - 1 to get the last inclusive value, and add 1 to the length
  // to prevent off-by-one.
  uint64_t block_length = (end_index - 1) / kMinfsBlockBits - start_block_offset + 1;
  zx_status_t status;
  status = bit_buffer.Initialize(handler_.get(), block_length, kMinfsBlockSize, "inode-bit-buffer");
  if (status != ZX_OK) {
    return fit::error(status);
  }

  status = loader.RunReadOperation(&bit_buffer, 0, start_block, block_length);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Cannot load allocation bits. err: %d\n", status);
    return fit::error(status);
  }

  // Once loaded, we treat the buffer as the entire inode bitmap and find the
  // new start index relative to it being in the first block. The element count
  // can be calculated normally.
  uint64_t buffer_offset = start_index % kMinfsBlockBits;
  uint64_t count = end_index - start_index;
  std::vector<uint64_t> allocated_indices;
  for (uint64_t i = 0; i < count; ++i) {
    if (GetBitmapElement(&bit_buffer, buffer_offset + i)) {
      allocated_indices.emplace_back(start_index + i);
    }
  }
  return fit::ok(allocated_indices);
}

// Since the scratch buffer is only a single block long, we check that the
// JournalSuperblock is small enough to load into the buffer.
static_assert(fs::kJournalMetadataBlocks == 1);

fit::result<fs::JournalInfo, zx_status_t> MinfsInspector::InspectJournalSuperblock() {
  Loader loader(handler_.get());
  zx_status_t status = loader.RunReadOperation(&buffer_, 0, JournalStartBlock(superblock_),
                                               fs::kJournalMetadataBlocks);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Cannot load journal superblock. err: %d\n", status);
    return fit::error(status);
  }
  return fit::ok(fs::GetJournalSuperblock(&buffer_));
}

template <>
fit::result<fs::JournalPrefix, zx_status_t> MinfsInspector::InspectJournalEntryAs(uint64_t index) {
  zx_status_t status = LoadJournalEntry(&buffer_, index);
  if (status != ZX_OK) {
    return fit::error(status);
  }
  return fit::ok(*reinterpret_cast<fs::JournalPrefix*>(buffer_.Data(0)));
}

template <>
fit::result<fs::JournalHeaderBlock, zx_status_t> MinfsInspector::InspectJournalEntryAs(
    uint64_t index) {
  zx_status_t status = LoadJournalEntry(&buffer_, index);
  if (status != ZX_OK) {
    return fit::error(status);
  }
  return fit::ok(*reinterpret_cast<fs::JournalHeaderBlock*>(buffer_.Data(0)));
}

template <>
fit::result<fs::JournalCommitBlock, zx_status_t> MinfsInspector::InspectJournalEntryAs(
    uint64_t index) {
  zx_status_t status = LoadJournalEntry(&buffer_, index);
  if (status != ZX_OK) {
    return fit::error(status);
  }
  return fit::ok(*reinterpret_cast<fs::JournalCommitBlock*>(buffer_.Data(0)));
}

fit::result<Superblock, zx_status_t> MinfsInspector::InspectBackupSuperblock() {
  Loader loader(handler_.get());
  uint32_t backup_location =
      GetMinfsFlagFvm(superblock_) ? kFvmSuperblockBackup : kNonFvmSuperblockBackup;
  zx_status_t status = loader.LoadSuperblock(backup_location, &buffer_);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Cannot load backup superblock. err: %d\n", status);
    return fit::error(status);
  }
  return fit::ok(GetSuperblock(&buffer_));
}

fit::result<void, zx_status_t> MinfsInspector::WriteSuperblock(Superblock superblock) {
  Loader loader(handler_.get());
  *static_cast<Superblock*>(buffer_.Data(0)) = superblock;
  zx_status_t status = loader.RunWriteOperation(&buffer_, 0, 0, 1);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Cannot write superblock. err: %d\n", status);
    return fit::error(status);
  }
  superblock_ = superblock;
  return fit::ok();
}

zx_status_t MinfsInspector::LoadJournalEntry(storage::VmoBuffer* buffer, uint64_t index) {
  Loader loader(handler_.get());
  uint64_t start_block = JournalStartBlock(superblock_) + fs::kJournalMetadataBlocks + index;
  zx_status_t status = loader.RunReadOperation(buffer, 0, start_block, 1);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Cannot load journal entry. err: %d\n", status);
  }
  return status;
}

}  // namespace minfs
