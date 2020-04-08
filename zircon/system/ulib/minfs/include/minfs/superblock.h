// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINFS_SUPERBLOCK_H_
#define MINFS_SUPERBLOCK_H_

#include <memory>

#include <fbl/macros.h>
#include <minfs/format.h>
#include <minfs/fsck.h>
#include <minfs/minfs.h>
#include <minfs/pending_work.h>

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
  ~SuperblockManager();
  DISALLOW_COPY_ASSIGN_AND_MOVE(SuperblockManager);

  static zx_status_t Create(block_client::BlockDevice* device, const Superblock* info,
                            uint32_t max_blocks, IntegrityCheck checks,
                            std::unique_ptr<SuperblockManager>* out);

  bool is_dirty() const { return dirty_; }

  const Superblock& Info() const { return *reinterpret_cast<const Superblock*>(mapping_.start()); }

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
  SuperblockManager(const Superblock* info, fzl::OwnedVmoMapper mapper);

  fzl::OwnedVmoMapper mapping_;
  bool dirty_ = false;
};

#else  // __Fuchsia__

class SuperblockManager {
 public:
  SuperblockManager() = delete;
  ~SuperblockManager();
  DISALLOW_COPY_ASSIGN_AND_MOVE(SuperblockManager);

  static zx_status_t Create(const Superblock* info, uint32_t max_blocks, IntegrityCheck checks,
                            std::unique_ptr<SuperblockManager>* out);

  bool is_dirty() const { return dirty_; }

  const Superblock& Info() const { return *reinterpret_cast<const Superblock*>(&info_blk_[0]); }

  // Acquire a pointer to the superblock, such that any
  // modifications will be carried out to persistent storage
  // the next time "Write" is invoked.
  Superblock* MutableInfo() {
    dirty_ = true;
    return reinterpret_cast<Superblock*>(&info_blk_[0]);
  }

  // Write the superblock/backup superblock back to persistent storage at respective locations.
  // If write_backup is kUpdate, also update the backup superblock.
  void Write(PendingWork* transaction, UpdateBackupSuperblock write_backup);

 private:
  SuperblockManager(const Superblock* info);

  uint8_t info_blk_[kMinfsBlockSize];
  bool dirty_ = false;
};

#endif

}  // namespace minfs

#endif  // MINFS_SUPERBLOCK_H_
