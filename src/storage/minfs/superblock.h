// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MINFS_SUPERBLOCK_H_
#define SRC_STORAGE_MINFS_SUPERBLOCK_H_

#include <cstdint>
#include <memory>

#include <fbl/macros.h>

#include "src/storage/minfs/format.h"
#include "src/storage/minfs/fsck.h"
#include "src/storage/minfs/minfs.h"
#include "src/storage/minfs/pending_work.h"

#ifdef __Fuchsia__
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/zx/vmo.h>

#include <block-client/cpp/block-device.h>
#endif

namespace minfs {

// SuperblockManager contains all filesystem-global metadata.
//
// It also contains mechanisms for updating this information
// on persistent storage. Although these fields may be
// updated from multiple threads (and |Write| may be invoked
// to push a snapshot of the superblock to persistent storage),
// caution should be taken to avoid Writing a snapshot of the
// superblock to disk while another thread has only partially
// updated the superblock.

#ifdef __Fuchsia__

class SuperblockManager {
 public:
  SuperblockManager() = delete;

  // Not copyable or movable
  SuperblockManager(const SuperblockManager&) = delete;
  SuperblockManager& operator=(const SuperblockManager&) = delete;
  SuperblockManager(SuperblockManager&&) = delete;
  SuperblockManager& operator=(SuperblockManager&&) = delete;

  ~SuperblockManager();

  static zx::status<std::unique_ptr<SuperblockManager>> Create(block_client::BlockDevice* device,
                                                               const Superblock& info,
                                                               uint32_t max_blocks,
                                                               IntegrityCheck checks);

  bool is_dirty() const { return dirty_; }

  const Superblock& Info() const { return *reinterpret_cast<const Superblock*>(mapping_.start()); }

  uint32_t BlockSize() const {
    // Either intentionally or unintenttionally, we do not want to change block
    // size to anything other than kMinfsBlockSize yet. This is because changing
    // block size might lead to format change and also because anything other
    // than 8k is not well tested. So assert when we find block size other
    // than 8k.
    ZX_ASSERT(Info().BlockSize() == kMinfsBlockSize);
    return Info().BlockSize();
  }
  // Acquire a pointer to the superblock, such that any
  // modifications will be carried out to persistent storage
  // the next time "Write" is invoked.
  Superblock* MutableInfo() {
    dirty_ = true;
    return reinterpret_cast<Superblock*>(mapping_.start());
  }

  // Write the superblock/backup superblock back to persistent storage at respective locations.
  // If write_backup is kUpdate, also update the backup superblock.
  void Write(PendingWork* transaction, UpdateBackupSuperblock write_backup);

 private:
  SuperblockManager(const Superblock& info, fzl::OwnedVmoMapper mapper);

  fzl::OwnedVmoMapper mapping_;
  bool dirty_ = false;
};

#else  // __Fuchsia__

class SuperblockManager {
 public:
  SuperblockManager() = delete;

  // Not copyable or movable
  SuperblockManager(const SuperblockManager&) = delete;
  SuperblockManager& operator=(const SuperblockManager&) = delete;
  SuperblockManager(SuperblockManager&&) = delete;
  SuperblockManager& operator=(SuperblockManager&&) = delete;

  ~SuperblockManager();

  static zx::status<std::unique_ptr<SuperblockManager>> Create(const Superblock& info,
                                                               uint32_t max_blocks,
                                                               IntegrityCheck checks);

  bool is_dirty() const { return dirty_; }

  const Superblock& Info() const { return *reinterpret_cast<const Superblock*>(info_blk_); }

  uint32_t BlockSize() const {
    // Either intentionally or unintenttionally, we do not want to change block
    // size to anything other than kMinfsBlockSize yet. This is because changing
    // block size might lead to format change and also because anything other
    // than 8k is not well tested. So assert when we find block size other
    // than 8k.
    ZX_ASSERT(Info().BlockSize() == kMinfsBlockSize);
    return Info().BlockSize();
  }
  // Acquire a pointer to the superblock, such that any
  // modifications will be carried out to persistent storage
  // the next time "Write" is invoked.
  Superblock* MutableInfo() {
    dirty_ = true;
    return reinterpret_cast<Superblock*>(info_blk_);
  }

  // Write the superblock/backup superblock back to persistent storage at respective locations.
  // If write_backup is kUpdate, also update the backup superblock.
  void Write(PendingWork* transaction, UpdateBackupSuperblock write_backup);

 private:
  SuperblockManager(const Superblock& info);

  uint8_t info_blk_[kMinfsBlockSize];
  bool dirty_ = false;
};

#endif

}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_SUPERBLOCK_H_
