// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/cksum.h>
#include <stdlib.h>
#include <string.h>

#include <memory>
#include <utility>

#include <bitmap/raw-bitmap.h>
#include <minfs/superblock.h>
#include <storage/buffer/block_buffer.h>

namespace minfs {

namespace {

// Trivial BlockBuffer that doesn't own the underlying buffer.
// TODO(47947): Remove this.
class UnownedBuffer : public storage::BlockBuffer {
 public:
  UnownedBuffer(const void* data) : data_(reinterpret_cast<const char*>(data)) {}
  ~UnownedBuffer() {}

  // BlockBuffer interface:
  size_t capacity() const final { return 0; }
  uint32_t BlockSize() const final { return 0; }
  vmoid_t vmoid() const final { return 0; }
  zx_handle_t Vmo() const final { return ZX_HANDLE_INVALID; }
  void* Data(size_t index) final { return const_cast<char*>(data_); }
  const void* Data(size_t index) const final { return data_; }

 private:
  const char* data_;  // Assumes that storage_ will only access the first block!.
};

}  // namespace

SuperblockManager::SuperblockManager(const Superblock* info) {
  memcpy(&info_blk_[0], info, sizeof(Superblock));
}

SuperblockManager::~SuperblockManager() = default;

// Static.
zx_status_t SuperblockManager::Create(const Superblock* info, uint32_t max_blocks,
                                      IntegrityCheck checks,
                                      std::unique_ptr<SuperblockManager>* out) {
  zx_status_t status = ZX_OK;
  if (checks == IntegrityCheck::kAll) {
    status = CheckSuperblock(info, max_blocks);
    if (status != ZX_OK) {
      FS_TRACE_ERROR("SuperblockManager::Create failed to check info: %d\n", status);
      return status;
    }
  }

  auto sb = std::unique_ptr<SuperblockManager>(new SuperblockManager(info));
  *out = std::move(sb);
  return ZX_OK;
}

void SuperblockManager::Write(PendingWork* transaction, UpdateBackupSuperblock write_backup) {
  UpdateChecksum(MutableInfo());
  UnownedBuffer data(&info_blk_[0]);

  storage::Operation operation = {
      .type = storage::OperationType::kWrite,
      .vmo_offset = 0,
      .dev_offset = kSuperblockStart,
      .length = 1,
  };
  transaction->EnqueueMetadata(operation, &data);

  if (write_backup == UpdateBackupSuperblock::kUpdate) {
    blk_t superblock_dev_offset = kNonFvmSuperblockBackup;

    if (MutableInfo()->flags & kMinfsFlagFVM) {
      superblock_dev_offset = kFvmSuperblockBackup;
    }

    storage::Operation operation = {
        .type = storage::OperationType::kWrite,
        .vmo_offset = 0,
        .dev_offset = superblock_dev_offset,
        .length = 1,
    };
    transaction->EnqueueMetadata(operation, &data);
  }
}

}  // namespace minfs
