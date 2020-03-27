// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "blobfs/blobfs_inspector.h"

#include <algorithm>

#include <fs/journal/internal/inspector_parser.h>
#include <fs/trace.h>

#include "parser.h"

namespace blobfs {

using disk_inspector::Loader;

BlobfsInspector::BlobfsInspector(std::unique_ptr<fs::TransactionHandler> handler,
                                 std::unique_ptr<disk_inspector::BufferFactory> buffer_factory)
    : handler_(std::move(handler)),
      buffer_factory_(std::move(buffer_factory)),
      loader_(handler_.get()) {}

fit::result<std::unique_ptr<BlobfsInspector>, zx_status_t> BlobfsInspector::Create(
    std::unique_ptr<fs::TransactionHandler> handler,
    std::unique_ptr<disk_inspector::BufferFactory> factory) {
  auto inspector =
      std::unique_ptr<BlobfsInspector>(new BlobfsInspector(std::move(handler), std::move(factory)));
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

zx_status_t BlobfsInspector::ReloadSuperblock() {
  zx_status_t status;
  status = loader_.RunReadOperation(buffer_.get(), 0, kSuperblockOffset, kBlobfsSuperblockBlocks);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Cannot load superblock. err: %d\n", status);
    return status;
  }
  superblock_ = GetSuperblock(buffer_.get());
  return ZX_OK;
}

Superblock BlobfsInspector::InspectSuperblock() { return superblock_; }

uint64_t BlobfsInspector::GetInodeCount() { return superblock_.inode_count; }

uint64_t BlobfsInspector::GetJournalEntryCount() {
  uint64_t journal_block_count = JournalBlocks(superblock_);
  // If there are no journal blocks, there cannot be any entries.
  if (journal_block_count == 0) {
    return 0;
  }
  return journal_block_count - fs::kJournalMetadataBlocks;
}

zx_status_t BlobfsInspector::LoadNodeElement(storage::BlockBuffer* buffer, uint64_t index) {
  uint64_t start_block_offset = index / kBlobfsInodesPerBlock;
  uint64_t start_block = NodeMapStartBlock(superblock_) + start_block_offset;
  zx_status_t status = loader_.RunReadOperation(buffer, 0, start_block, 1);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Cannot load node element. err: %d\n", status);
  }
  return status;
}

fit::result<std::vector<Inode>, zx_status_t> BlobfsInspector::InspectInodeRange(
    uint64_t start_index, uint64_t end_index) {
  ZX_ASSERT(end_index > start_index);
  // Since there are multiple inodes in a block, we first perform calculations
  // to find the block range of only the desired inode range to load.
  uint64_t start_block_offset = start_index / kBlobfsInodesPerBlock;
  uint64_t start_block = NodeMapStartBlock(superblock_) + start_block_offset;
  // Because the end index is exclusive, we calculate the length based on
  // end index - 1 to get the last inclusive value, and add 1 to the length
  // to prevent off-by-one.
  uint64_t block_length = (end_index - 1) / kBlobfsInodesPerBlock - start_block_offset + 1;

  auto result = buffer_factory_->CreateBuffer(block_length);
  if (result.is_error()) {
    return result.take_error_result();
  }
  std::unique_ptr<storage::BlockBuffer> inode_buffer = result.take_value();

  zx_status_t status = loader_.RunReadOperation(inode_buffer.get(), 0, start_block, block_length);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Cannot load inode. err: %d\n", status);
    return fit::error(status);
  }

  // Once loaded, we treat the buffer as the entire inode table and find the
  // new start index relative to it being in the first block. The element count
  // can be calculated normally.
  uint64_t buffer_offset = start_index % kBlobfsInodesPerBlock;
  uint64_t count = end_index - start_index;
  std::vector<Inode> inodes;
  for (uint64_t i = 0; i < count; ++i) {
    inodes.emplace_back(GetInodeElement(inode_buffer.get(), buffer_offset + i));
  }
  return fit::ok(inodes);
}

// Since the scratch buffer is only a single block long, we check that the
// JournalSuperblock is small enough to load into the buffer.
static_assert(fs::kJournalMetadataBlocks == 1);

fit::result<fs::JournalInfo, zx_status_t> BlobfsInspector::InspectJournalSuperblock() {
  zx_status_t status = loader_.RunReadOperation(buffer_.get(), 0, JournalStartBlock(superblock_),
                                                fs::kJournalMetadataBlocks);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Cannot load journal superblock. err: %d\n", status);
    return fit::error(status);
  }
  return fit::ok(fs::GetJournalSuperblock(buffer_.get()));
}

template <>
fit::result<fs::JournalPrefix, zx_status_t> BlobfsInspector::InspectJournalEntryAs(uint64_t index) {
  zx_status_t status = LoadJournalEntry(buffer_.get(), index);
  if (status != ZX_OK) {
    return fit::error(status);
  }
  return fit::ok(*reinterpret_cast<fs::JournalPrefix*>(buffer_->Data(0)));
}

template <>
fit::result<fs::JournalHeaderBlock, zx_status_t> BlobfsInspector::InspectJournalEntryAs(
    uint64_t index) {
  zx_status_t status = LoadJournalEntry(buffer_.get(), index);
  if (status != ZX_OK) {
    return fit::error(status);
  }
  return fit::ok(*reinterpret_cast<fs::JournalHeaderBlock*>(buffer_->Data(0)));
}

template <>
fit::result<fs::JournalCommitBlock, zx_status_t> BlobfsInspector::InspectJournalEntryAs(
    uint64_t index) {
  zx_status_t status = LoadJournalEntry(buffer_.get(), index);
  if (status != ZX_OK) {
    return fit::error(status);
  }
  return fit::ok(*reinterpret_cast<fs::JournalCommitBlock*>(buffer_->Data(0)));
}

fit::result<void, zx_status_t> BlobfsInspector::WriteSuperblock(Superblock superblock) {
  *static_cast<Superblock*>(buffer_->Data(0)) = superblock;
  zx_status_t status = loader_.RunWriteOperation(buffer_.get(), 0, 0, 1);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Cannot write superblock. err: %d\n", status);
    return fit::error(status);
  }
  superblock_ = superblock;
  return fit::ok();
}

zx_status_t BlobfsInspector::LoadJournalEntry(storage::BlockBuffer* buffer, uint64_t index) {
  uint64_t start_block = JournalStartBlock(superblock_) + fs::kJournalMetadataBlocks + index;
  zx_status_t status = loader_.RunReadOperation(buffer, 0, start_block, 1);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Cannot load journal entry. err: %d\n", status);
  }
  return status;
}

}  // namespace blobfs
