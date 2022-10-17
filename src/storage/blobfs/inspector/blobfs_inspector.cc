// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/blobfs_inspector.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <algorithm>

#include "src/lib/storage/vfs/cpp/journal/inspector_parser.h"
#include "src/storage/blobfs/inspector/parser.h"

namespace blobfs {

BlobfsInspector::BlobfsInspector(std::unique_ptr<fs::TransactionHandler> handler,
                                 std::unique_ptr<disk_inspector::BufferFactory> buffer_factory)
    : handler_(std::move(handler)),
      buffer_factory_(std::move(buffer_factory)),
      loader_(handler_.get()) {}

zx::result<std::unique_ptr<BlobfsInspector>> BlobfsInspector::Create(
    std::unique_ptr<fs::TransactionHandler> handler,
    std::unique_ptr<disk_inspector::BufferFactory> factory) {
  auto inspector =
      std::unique_ptr<BlobfsInspector>(new BlobfsInspector(std::move(handler), std::move(factory)));
  auto result = inspector->buffer_factory_->CreateBuffer(1);
  if (result.is_error()) {
    return zx::error(result.take_error());
  }
  inspector->buffer_ = result.take_value();
  zx_status_t status = inspector->ReloadSuperblock();
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(std::move(inspector));
}

zx_status_t BlobfsInspector::ReloadSuperblock() {
  zx_status_t status;
  status = loader_.RunReadOperation(buffer_.get(), 0, kSuperblockOffset, kBlobfsSuperblockBlocks);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot load superblock. Error: " << zx_status_get_string(status);
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

zx::result<std::vector<Inode>> BlobfsInspector::InspectInodeRange(uint64_t start_index,
                                                                  uint64_t end_index) {
  ZX_ASSERT(end_index > start_index);
  // Since there are multiple inodes in a block, we first perform calculations to find the block
  // range of only the desired inode range to load.
  uint64_t start_block_offset = start_index / kBlobfsInodesPerBlock;
  uint64_t start_block = NodeMapStartBlock(superblock_) + start_block_offset;
  // Because the end index is exclusive, we calculate the length based on end index - 1 to get the
  // last inclusive value, and add 1 to the length to prevent off-by-one.
  uint64_t block_length = (end_index - 1) / kBlobfsInodesPerBlock - start_block_offset + 1;

  auto result = buffer_factory_->CreateBuffer(block_length);
  if (result.is_error()) {
    return zx::error(result.take_error());
  }
  std::unique_ptr<storage::BlockBuffer> inode_buffer = result.take_value();

  zx_status_t status = loader_.RunReadOperation(inode_buffer.get(), 0, start_block, block_length);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot load inode. Error: " << zx_status_get_string(status);
    return zx::error(status);
  }

  // Once loaded, we treat the buffer as the entire inode table and find the new start index
  // relative to it being in the first block. The element count can be calculated normally.
  uint64_t buffer_offset = start_index % kBlobfsInodesPerBlock;
  uint64_t count = end_index - start_index;
  std::vector<Inode> inodes;
  for (uint64_t i = 0; i < count; ++i) {
    inodes.emplace_back(GetInodeElement(inode_buffer.get(), buffer_offset + i));
  }
  return zx::ok(inodes);
}

// Since the scratch buffer is only a single block long, we check that the JournalSuperblock is
// small enough to load into the buffer.
static_assert(fs::kJournalMetadataBlocks == 1);

zx::result<fs::JournalInfo> BlobfsInspector::InspectJournalSuperblock() {
  zx_status_t status = loader_.RunReadOperation(buffer_.get(), 0, JournalStartBlock(superblock_),
                                                fs::kJournalMetadataBlocks);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot load journal superblock. Error: " << zx_status_get_string(status);
    return zx::error(status);
  }
  return zx::ok(fs::GetJournalSuperblock(buffer_.get()));
}

template <>
zx::result<fs::JournalPrefix> BlobfsInspector::InspectJournalEntryAs(uint64_t index) {
  zx_status_t status = LoadJournalEntry(buffer_.get(), index);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(*reinterpret_cast<fs::JournalPrefix*>(buffer_->Data(0)));
}

template <>
zx::result<fs::JournalHeaderBlock> BlobfsInspector::InspectJournalEntryAs(uint64_t index) {
  zx_status_t status = LoadJournalEntry(buffer_.get(), index);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(*reinterpret_cast<fs::JournalHeaderBlock*>(buffer_->Data(0)));
}

template <>
zx::result<fs::JournalCommitBlock> BlobfsInspector::InspectJournalEntryAs(uint64_t index) {
  zx_status_t status = LoadJournalEntry(buffer_.get(), index);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(*reinterpret_cast<fs::JournalCommitBlock*>(buffer_->Data(0)));
}

zx::result<std::vector<uint64_t>> BlobfsInspector::InspectDataBlockAllocatedInRange(
    uint64_t start_index, uint64_t end_index) {
  ZX_ASSERT(end_index > start_index);
  // Since there are multiple bits in a block, we first perform calculations to find the block range
  // of only the desired bit range to load.
  uint64_t start_block_offset = start_index / kBlobfsBlockBits;
  uint64_t start_block = BlockMapStartBlock(superblock_) + start_block_offset;
  // Because the end index is exclusive, we calculate the length based on end index - 1 to get the
  // last inclusive value, and add 1 to the length to prevent off-by-one.
  uint64_t block_length = (end_index - 1) / kBlobfsBlockBits - start_block_offset + 1;

  auto result = buffer_factory_->CreateBuffer(block_length);
  if (result.is_error()) {
    return zx::error(result.take_error());
  }
  std::unique_ptr<storage::BlockBuffer> bit_buffer = result.take_value();

  zx_status_t status = loader_.RunReadOperation(bit_buffer.get(), 0, start_block, block_length);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot load allocation bits. Error: " << zx_status_get_string(status);
    return zx::error(status);
  }

  // Once loaded, we treat the buffer as the entire inode bitmap and find the new start index
  // relative to it being in the first block. The element count can be calculated normally.
  uint64_t buffer_offset = start_index % kBlobfsBlockBits;
  uint64_t count = end_index - start_index;
  std::vector<uint64_t> allocated_indices;
  for (uint64_t i = 0; i < count; ++i) {
    if (GetBitmapElement(bit_buffer.get(), buffer_offset + i)) {
      allocated_indices.emplace_back(start_index + i);
    }
  }
  return zx::ok(allocated_indices);
}

zx::result<> BlobfsInspector::WriteSuperblock(Superblock superblock) {
  *static_cast<Superblock*>(buffer_->Data(0)) = superblock;
  zx_status_t status = loader_.RunWriteOperation(buffer_.get(), 0, 0, 1);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot write superblock. Error: " << zx_status_get_string(status);
    return zx::error(status);
  }
  superblock_ = superblock;
  return zx::ok();
}

zx::result<> BlobfsInspector::WriteInodes(std::vector<Inode> inodes, uint64_t start_index) {
  uint64_t end_index = start_index + inodes.size();
  // Since there are multiple inodes in a block, we first perform calculations to find the block
  // range of only the desired inode range to load.
  uint64_t start_block_offset = start_index / kBlobfsInodesPerBlock;
  uint64_t start_block = NodeMapStartBlock(superblock_) + start_block_offset;
  // Because the end index is exclusive, we calculate the length based on end index - 1 to get the
  // last inclusive value, and add 1 to the length to prevent off-by-one.
  uint64_t block_length = (end_index - 1) / kBlobfsInodesPerBlock - start_block_offset + 1;

  auto result = buffer_factory_->CreateBuffer(block_length);
  if (result.is_error()) {
    return zx::error(result.take_error());
  }
  std::unique_ptr<storage::BlockBuffer> inode_buffer = result.take_value();

  // We still need to perform a read in case the inode range to write is not aligned on block
  // boundaries.
  zx_status_t status = loader_.RunReadOperation(inode_buffer.get(), 0, start_block, block_length);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot load inodes. Error: " << zx_status_get_string(status);
    return zx::error(status);
  }

  // Once loaded, we treat the buffer as the entire inode table and find the new start index
  // relative to it being in the first block. The element count can be calculated normally.
  uint64_t buffer_offset = start_index % kBlobfsInodesPerBlock;
  uint64_t count = end_index - start_index;
  for (uint64_t i = 0; i < count; ++i) {
    WriteInodeElement(inode_buffer.get(), inodes[i], buffer_offset + i);
  }

  status = loader_.RunWriteOperation(inode_buffer.get(), 0, start_block, block_length);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot write inodes. Error: " << zx_status_get_string(status);
    return zx::error(status);
  }
  return zx::ok();
}

zx::result<> BlobfsInspector::WriteJournalSuperblock(fs::JournalInfo journal_info) {
  *reinterpret_cast<fs::JournalInfo*>(buffer_->Data(0)) = journal_info;
  zx_status_t status = loader_.RunWriteOperation(buffer_.get(), 0, JournalStartBlock(superblock_),
                                                 fs::kJournalMetadataBlocks);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot write journal superblock. Error: " << zx_status_get_string(status);
    return zx::error(status);
  }
  return zx::ok();
}

zx::result<> BlobfsInspector::WriteJournalEntryBlocks(storage::BlockBuffer* buffer,
                                                      uint64_t start_index) {
  uint64_t start_block = JournalStartBlock(superblock_) + fs::kJournalMetadataBlocks + start_index;
  zx_status_t status = loader_.RunWriteOperation(buffer, 0, start_block, buffer->capacity());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot write journal entries. Error: " << zx_status_get_string(status);
    return zx::error(status);
  }
  return zx::ok();
}

zx::result<> BlobfsInspector::WriteDataBlockAllocationBits(bool value, uint64_t start_index,
                                                           uint64_t end_index) {
  ZX_ASSERT(end_index > start_index);
  // Since there are multiple bits in a block, we first perform calculations to find the block range
  // of only the desired bit range to load.
  uint64_t start_block_offset = start_index / kBlobfsBlockBits;
  uint64_t start_block = BlockMapBlocks(superblock_) + start_block_offset;
  // Because the end index is exclusive, we calculate the length based on end index - 1 to get the
  // last inclusive value, and add 1 to the length to prevent off-by-one.
  uint64_t block_length = (end_index - 1) / kBlobfsBlockBits - start_block_offset + 1;

  auto result = buffer_factory_->CreateBuffer(block_length);
  if (result.is_error()) {
    return zx::error(result.take_error());
  }
  std::unique_ptr<storage::BlockBuffer> bit_buffer = result.take_value();

  // We still need to perform a read in case the bit range to write is not aligned on block
  // boundaries.
  zx_status_t status = loader_.RunReadOperation(bit_buffer.get(), 0, start_block, block_length);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot load allocation bits. Error: " << zx_status_get_string(status);
    return zx::error(status);
  }

  // Once loaded, we treat the buffer as the entire bit bitmap and find the new start index relative
  // to it being in the first block. The element count can be calculated normally.
  uint64_t buffer_offset = start_index % kBlobfsBlockBits;
  uint64_t count = end_index - start_index;
  std::vector<uint64_t> allocated_indices;
  for (uint64_t i = 0; i < count; ++i) {
    WriteBitmapElement(bit_buffer.get(), value, buffer_offset + i);
  }

  status = loader_.RunWriteOperation(bit_buffer.get(), 0, start_block, block_length);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot load allocation bits. Error: " << zx_status_get_string(status);
    return zx::error(status);
  }
  return zx::ok();
}

zx::result<> BlobfsInspector::WriteDataBlocks(storage::BlockBuffer* buffer, uint64_t start_index) {
  uint64_t start_block = DataStartBlock(superblock_) + start_index;
  zx_status_t status = loader_.RunWriteOperation(buffer, 0, start_block, buffer->capacity());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot write data blocks. Error: " << zx_status_get_string(status);
    return zx::error(status);
  }
  return zx::ok();
}

zx_status_t BlobfsInspector::LoadNodeElement(storage::BlockBuffer* buffer, uint64_t index) {
  uint64_t start_block_offset = index / kBlobfsInodesPerBlock;
  uint64_t start_block = NodeMapStartBlock(superblock_) + start_block_offset;
  zx_status_t status = loader_.RunReadOperation(buffer, 0, start_block, 1);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot load node element. Error: " << zx_status_get_string(status);
  }
  return status;
}

zx_status_t BlobfsInspector::LoadJournalEntry(storage::BlockBuffer* buffer, uint64_t index) {
  uint64_t start_block = JournalStartBlock(superblock_) + fs::kJournalMetadataBlocks + index;
  zx_status_t status = loader_.RunReadOperation(buffer, 0, start_block, 1);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot load journal entry. Error: " << zx_status_get_string(status);
  }
  return status;
}

}  // namespace blobfs
