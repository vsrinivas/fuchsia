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
  status = inspector->ReloadSuperblock();
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Cannot load superblock to initialize the inspector. err: %d\n", status);
    return status;
  }
  inspector->ReloadMetadataFromSuperblock();
  out->reset(inspector);
  return ZX_OK;
}

zx_status_t MinfsInspector::ReloadSuperblock() {
  Loader loader(handler_.get());
  superblock_ = std::make_unique<storage::VmoBuffer>();
  zx_status_t status = ZX_OK;
  status = superblock_->Initialize(handler_.get(), kSuperblockBlocks, kMinfsBlockSize,
                                   "superblock-buffer");
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Cannot create superblock buffer. err: %d\n", status);
    return status;
  }
  if ((status = loader.LoadSuperblock(kSuperblockStart, superblock_.get())) != ZX_OK) {
    FS_TRACE_ERROR("Cannot load superblock. err: %d\n", status);
  }
  return status;
}

void MinfsInspector::ReloadMetadataFromSuperblock() {
  Loader loader(handler_.get());
  zx_status_t status;
  Superblock superblock = GetSuperblock(superblock_.get());

  inode_bitmap_ = std::make_unique<storage::VmoBuffer>();
  inode_table_ = std::make_unique<storage::VmoBuffer>();
  journal_ = std::make_unique<storage::VmoBuffer>();

  status = inode_bitmap_->Initialize(handler_.get(), InodeBitmapBlocks(superblock), kMinfsBlockSize,
                                     "inode-bitmap-buffer");
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Cannot create inode bitmap buffer. err: %d\n", status);
  } else {
    if ((status = loader.LoadInodeBitmap(superblock, inode_bitmap_.get())) != ZX_OK) {
      FS_TRACE_ERROR("Cannot load inode bitmap. Some data may be garbage. err: %d\n", status);
    }
  }

  status = inode_table_->Initialize(handler_.get(), InodeBlocks(superblock), kMinfsBlockSize,
                                    "inode-table-buffer");
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Cannot create inode table buffer. err: %d\n", status);
  } else {
    if ((status = loader.LoadInodeTable(superblock, inode_table_.get())) != ZX_OK) {
      FS_TRACE_ERROR("Cannot load inode table. Some data may be garbage. err: %d\n", status);
    }
  }

  status = journal_->Initialize(handler_.get(), JournalBlocks(superblock), kMinfsBlockSize,
                                "journal-buffer");
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Cannot create journal buffer. err: %d\n", status);
  } else {
    if ((status = loader.LoadJournal(superblock, journal_.get())) != ZX_OK) {
      FS_TRACE_ERROR("Cannot load journal. Some data may be garbage. err: %d\n", status);
    }
  }
}

Superblock MinfsInspector::InspectSuperblock() { return GetSuperblock(superblock_.get()); }

uint64_t MinfsInspector::GetInodeCount() { return inode_table_->capacity() * kMinfsInodesPerBlock; }

uint64_t MinfsInspector::GetInodeBitmapCount() {
  return inode_bitmap_->capacity() * inode_bitmap_->BlockSize() * CHAR_BIT;
}

Inode MinfsInspector::InspectInode(uint64_t index) {
  return GetInodeElement(inode_table_.get(), index);
}

bool MinfsInspector::CheckInodeAllocated(uint64_t index) {
  return GetBitmapElement(inode_bitmap_.get(), index);
}

fs::JournalInfo MinfsInspector::InspectJournalSuperblock() {
  if (journal_->capacity() == 0) {
    fs::JournalInfo info = {};
    return info;
  }
  return fs::GetJournalSuperblock(journal_.get());
}

uint64_t MinfsInspector::GetJournalEntryCount() {
  // Case in which the journal buffer could not be initialized. We treat it as though
  // there are journal entries.
  if (journal_->capacity() == 0) {
    return 0;
  }
  return journal_->capacity() - fs::kJournalMetadataBlocks;
}

fs::JournalPrefix MinfsInspector::InspectJournalPrefix(uint64_t index) {
  std::array<uint8_t, fs::kJournalBlockSize> block = fs::GetBlockEntry(journal_.get(), index);
  return *reinterpret_cast<fs::JournalPrefix*>(block.data());
}

fs::JournalHeaderBlock MinfsInspector::InspectJournalHeader(uint64_t index) {
  std::array<uint8_t, fs::kJournalBlockSize> block = fs::GetBlockEntry(journal_.get(), index);
  return *reinterpret_cast<fs::JournalHeaderBlock*>(block.data());
}

fs::JournalCommitBlock MinfsInspector::InspectJournalCommit(uint64_t index) {
  std::array<uint8_t, fs::kJournalBlockSize> block = fs::GetBlockEntry(journal_.get(), index);
  return *reinterpret_cast<fs::JournalCommitBlock*>(block.data());
}

zx_status_t MinfsInspector::InspectBackupSuperblock(Superblock* backup) {
  Loader loader(handler_.get());
  auto buffer = std::make_unique<storage::VmoBuffer>();
  zx_status_t status;
  status = buffer->Initialize(handler_.get(), 1, kMinfsBlockSize, "backup-superblock-buffer");
  if (status != ZX_OK) {
    return status;
  }
  Superblock sb = GetSuperblock(superblock_.get());
  uint32_t backup_location = GetMinfsFlagFvm(sb) ? kFvmSuperblockBackup : kNonFvmSuperblockBackup;
  if ((status = loader.LoadSuperblock(backup_location, buffer.get())) != ZX_OK) {
    return status;
  }
  *backup = GetSuperblock(buffer.get());
  return ZX_OK;
}

}  // namespace minfs
