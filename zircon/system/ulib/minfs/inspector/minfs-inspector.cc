// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minfs/minfs-inspector.h"

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
  out->reset(new MinfsInspector(std::move(handler)));
  status = out->get()->Initialize();
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Cannot initialize minfs inspector. Some data may be garbage. err: %d\n",
                   status);
    return status;
  }
  return ZX_OK;
}

zx_status_t MinfsInspector::Initialize() {
  Loader loader(handler_.get());
  superblock_ = std::make_unique<storage::VmoBuffer>();
  zx_status_t status;
  status = superblock_->Initialize(handler_.get(), kSuperblockBlocks, kMinfsBlockSize,
                                   "superblock-buffer");
  if (status != ZX_OK) {
    return status;
  }
  if ((status = loader.LoadSuperblock(kSuperblockStart, superblock_.get())) != ZX_OK) {
    FS_TRACE_ERROR("Cannot load superblock. Some data may be garbage. err: %d\n", status);
    return status;
  }

  Superblock superblock = GetSuperblock(superblock_.get());

  inode_bitmap_ = std::make_unique<storage::VmoBuffer>();
  status = inode_bitmap_->Initialize(handler_.get(), InodeBitmapBlocks(superblock), kMinfsBlockSize,
                                     "inode-bitmap-buffer");
  if (status != ZX_OK) {
    return status;
  }
  if ((status = loader.LoadInodeBitmap(superblock, inode_bitmap_.get())) != ZX_OK) {
    FS_TRACE_ERROR("Cannot load inode bitmap. Some data may be garbage. err: %d\n", status);
    return status;
  }

  inode_table_ = std::make_unique<storage::VmoBuffer>();
  status = inode_table_->Initialize(handler_.get(), InodeBlocks(superblock), kMinfsBlockSize,
                                    "inode-table-buffer");
  if (status != ZX_OK) {
    return status;
  }
  if ((status = loader.LoadInodeTable(superblock, inode_table_.get())) != ZX_OK) {
    FS_TRACE_ERROR("Cannot load inode table. Some data may be garbage. err: %d\n", status);
    return status;
  }

  journal_ = std::make_unique<storage::VmoBuffer>();
  status = journal_->Initialize(handler_.get(), JournalBlocks(superblock), kMinfsBlockSize,
                                "journal-buffer");
  if (status != ZX_OK) {
    return status;
  }
  if ((status = loader.LoadJournal(superblock, journal_.get())) != ZX_OK) {
    FS_TRACE_ERROR("Cannot load journal. Some data may be garbage. err: %d\n", status);
    return status;
  }

  return ZX_OK;
}

Superblock MinfsInspector::InspectSuperblock() { return GetSuperblock(superblock_.get()); }

Inode MinfsInspector::InspectInode(uint64_t index) {
  return GetInodeElement(inode_table_.get(), index);
}

uint64_t MinfsInspector::GetInodeCount() {
  Superblock superblock = GetSuperblock(superblock_.get());
  return superblock.inode_count;
}

bool MinfsInspector::CheckInodeAllocated(uint64_t index) {
  return GetBitmapElement(inode_bitmap_.get(), index);
}

fs::JournalInfo MinfsInspector::InspectJournalSuperblock() {
  return fs::GetJournalSuperblock(journal_.get());
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
