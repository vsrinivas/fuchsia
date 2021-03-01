// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/minfs_inspector.h"

#include <lib/syslog/cpp/macros.h>

#include <algorithm>

#include <disk_inspector/inspector_transaction_handler.h>
#include <disk_inspector/vmo_buffer_factory.h>

#include "src/lib/storage/vfs/cpp/journal/inspector_parser.h"
#include "src/storage/minfs/format.h"
#include "src/storage/minfs/inspector/loader.h"
#include "src/storage/minfs/inspector/parser.h"

namespace minfs {

MinfsInspector::MinfsInspector(std::unique_ptr<fs::TransactionHandler> handler,
                               std::unique_ptr<disk_inspector::BufferFactory> buffer_factory)
    : handler_(std::move(handler)), buffer_factory_(std::move(buffer_factory)) {}

fit::result<std::unique_ptr<MinfsInspector>, zx_status_t> MinfsInspector::Create(
    std::unique_ptr<fs::TransactionHandler> handler,
    std::unique_ptr<disk_inspector::BufferFactory> factory) {
  auto inspector =
      std::unique_ptr<MinfsInspector>(new MinfsInspector(std::move(handler), std::move(factory)));
  auto result = inspector->buffer_factory_->CreateBuffer(1);
  if (result.is_error()) {
    return result.take_error_result();
  }
  inspector->buffer_ = result.take_value();
  zx_status_t status = inspector->ReloadSuperblock();
  if (status != ZX_OK) {
    return fit::error(status);
  }
  return fit::ok(std::move(inspector));
}

zx_status_t MinfsInspector::ReloadSuperblock() {
  Loader loader(handler_.get());
  zx_status_t status;
  status = loader.LoadSuperblock(kSuperblockStart, buffer_.get());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot load superblock. err: " << status;
    return status;
  }
  superblock_ = GetSuperblock(buffer_.get());
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

  // Since there are multiple inodes in a block, we first perform calculations
  // to find the block range of only the desired inode range to load.
  uint64_t start_block_offset = start_index / kMinfsInodesPerBlock;
  uint64_t start_block = superblock_.ino_block + start_block_offset;
  // Because the end index is exclusive, we calculate the length based on
  // end index - 1 to get the last inclusive value, and add 1 to the length
  // to prevent off-by-one.
  uint64_t block_length = (end_index - 1) / kMinfsInodesPerBlock - start_block_offset + 1;

  auto result = buffer_factory_->CreateBuffer(block_length);
  if (result.is_error()) {
    return result.take_error_result();
  }
  std::unique_ptr<storage::BlockBuffer> inode_buffer = result.take_value();

  zx_status_t status = loader.RunReadOperation(inode_buffer.get(), 0, start_block, block_length);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot load inode. err: " << status;
    return fit::error(status);
  }

  // Once loaded, we treat the buffer as the entire inode table and find the
  // new start index relative to it being in the first block. The element count
  // can be calculated normally.
  uint64_t buffer_offset = start_index % kMinfsInodesPerBlock;
  uint64_t count = end_index - start_index;
  std::vector<Inode> inodes;
  for (uint64_t i = 0; i < count; ++i) {
    inodes.emplace_back(GetInodeElement(inode_buffer.get(), buffer_offset + i));
  }
  return fit::ok(inodes);
}

fit::result<std::vector<uint64_t>, zx_status_t> MinfsInspector::InspectInodeAllocatedInRange(
    uint64_t start_index, uint64_t end_index) {
  ZX_ASSERT(end_index > start_index);
  Loader loader(handler_.get());
  // Since there are multiple bits in a block, we first perform calculations
  // to find the block range of only the desired bit range to load.
  uint64_t start_block_offset = start_index / kMinfsBlockBits;
  uint64_t start_block = superblock_.ibm_block + start_block_offset;
  // Because the end index is exclusive, we calculate the length based on
  // end index - 1 to get the last inclusive value, and add 1 to the length
  // to prevent off-by-one.
  uint64_t block_length = (end_index - 1) / kMinfsBlockBits - start_block_offset + 1;

  auto result = buffer_factory_->CreateBuffer(block_length);
  if (result.is_error()) {
    return fit::error(result.take_error());
  }
  std::unique_ptr<storage::BlockBuffer> bit_buffer = result.take_value();

  zx_status_t status = loader.RunReadOperation(bit_buffer.get(), 0, start_block, block_length);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot load allocation bits. err: " << status;
    return fit::error(status);
  }

  // Once loaded, we treat the buffer as the entire inode bitmap and find the
  // new start index relative to it being in the first block. The element count
  // can be calculated normally.
  uint64_t buffer_offset = start_index % kMinfsBlockBits;
  uint64_t count = end_index - start_index;
  std::vector<uint64_t> allocated_indices;
  for (uint64_t i = 0; i < count; ++i) {
    if (GetBitmapElement(bit_buffer.get(), buffer_offset + i)) {
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
  zx_status_t status = loader.RunReadOperation(buffer_.get(), 0, JournalStartBlock(superblock_),
                                               fs::kJournalMetadataBlocks);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot load journal superblock. err: " << status;
    return fit::error(status);
  }
  return fit::ok(fs::GetJournalSuperblock(buffer_.get()));
}

template <>
fit::result<fs::JournalPrefix, zx_status_t> MinfsInspector::InspectJournalEntryAs(uint64_t index) {
  zx_status_t status = LoadJournalEntry(buffer_.get(), index);
  if (status != ZX_OK) {
    return fit::error(status);
  }
  return fit::ok(*reinterpret_cast<fs::JournalPrefix*>(buffer_->Data(0)));
}

template <>
fit::result<fs::JournalHeaderBlock, zx_status_t> MinfsInspector::InspectJournalEntryAs(
    uint64_t index) {
  zx_status_t status = LoadJournalEntry(buffer_.get(), index);
  if (status != ZX_OK) {
    return fit::error(status);
  }
  return fit::ok(*reinterpret_cast<fs::JournalHeaderBlock*>(buffer_->Data(0)));
}

template <>
fit::result<fs::JournalCommitBlock, zx_status_t> MinfsInspector::InspectJournalEntryAs(
    uint64_t index) {
  zx_status_t status = LoadJournalEntry(buffer_.get(), index);
  if (status != ZX_OK) {
    return fit::error(status);
  }
  return fit::ok(*reinterpret_cast<fs::JournalCommitBlock*>(buffer_->Data(0)));
}

fit::result<Superblock, zx_status_t> MinfsInspector::InspectBackupSuperblock() {
  Loader loader(handler_.get());
  uint32_t backup_location =
      superblock_.GetFlagFvm() ? kFvmSuperblockBackup : kNonFvmSuperblockBackup;
  zx_status_t status = loader.LoadSuperblock(backup_location, buffer_.get());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot load backup superblock. err: " << status;
    return fit::error(status);
  }
  return fit::ok(GetSuperblock(buffer_.get()));
}

fit::result<void, zx_status_t> MinfsInspector::WriteSuperblock(Superblock superblock) {
  Loader loader(handler_.get());
  *static_cast<Superblock*>(buffer_->Data(0)) = superblock;
  zx_status_t status = loader.RunWriteOperation(buffer_.get(), 0, 0, 1);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot write superblock. err: " << status;
    return fit::error(status);
  }
  superblock_ = superblock;
  return fit::ok();
}

zx_status_t MinfsInspector::LoadJournalEntry(storage::BlockBuffer* buffer, uint64_t index) {
  Loader loader(handler_.get());
  uint64_t start_block = JournalStartBlock(superblock_) + fs::kJournalMetadataBlocks + index;
  zx_status_t status = loader.RunReadOperation(buffer, 0, start_block, 1);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot load journal entry. err: " << status;
  }
  return status;
}

}  // namespace minfs
