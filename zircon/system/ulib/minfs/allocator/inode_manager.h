// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file describes the structure used to access inodes.
// Currently, this structure is implemented on-disk as a table.

#ifndef ZIRCON_SYSTEM_ULIB_MINFS_ALLOCATOR_INODE_MANAGER_H_
#define ZIRCON_SYSTEM_ULIB_MINFS_ALLOCATOR_INODE_MANAGER_H_

#include <memory>

#include <fbl/macros.h>
#include <minfs/format.h>

#ifdef __Fuchsia__
#include <lib/fzl/resizeable-vmo-mapper.h>

#include <block-client/cpp/block-device.h>
#endif

#include "allocator.h"

namespace minfs {

class InspectableInodeManager {
 public:
  virtual ~InspectableInodeManager() {}

  // Gets immutable reference to the inode allocator.
  virtual const Allocator* GetInodeAllocator() const = 0;

  // Loads the inode from storage.
  virtual void Load(ino_t inode_num, Inode* out) const = 0;

  // Checks if the inode is allocated.
  virtual bool CheckAllocated(uint32_t inode_num) const = 0;
};

// InodeManager is responsible for owning the persistent storage for inodes.
//
// It can be used to Load and Update inodes on storage.
// Additionally, it is responsible for allocating and freeing inodes.
class InodeManager : public InspectableInodeManager {
 public:
  InodeManager() = delete;
  DISALLOW_COPY_ASSIGN_AND_MOVE(InodeManager);
  ~InodeManager() {}

#ifdef __Fuchsia__
  static zx_status_t Create(block_client::BlockDevice* device, SuperblockManager* sb,
                            fs::BufferedOperationsBuilder* builder, AllocatorMetadata metadata,
                            blk_t start_block, size_t inodes, std::unique_ptr<InodeManager>* out);
#else
  static zx_status_t Create(Bcache* bc, SuperblockManager* sb,
                            fs::BufferedOperationsBuilder* builder, AllocatorMetadata metadata,
                            blk_t start_block, size_t inodes, std::unique_ptr<InodeManager>* out);
#endif

  // Reserve |inodes| inodes in the allocator.
  zx_status_t Reserve(PendingWork* transaction, size_t inodes, AllocatorReservation* reservation) {
    return reservation->Initialize(transaction, inodes, inode_allocator_.get());
  }

  // Free an inode.
  void Free(PendingWork* transaction, size_t index) { inode_allocator_->Free(transaction, index); }

  // Persist the inode to storage.
  void Update(PendingWork* transaction, ino_t ino, const Inode* inode);

  // InspectableInodeManager interface:
  const Allocator* GetInodeAllocator() const final;

  void Load(ino_t ino, Inode* out) const final;

  bool CheckAllocated(uint32_t inode_num) const final {
    return inode_allocator_->CheckAllocated(inode_num);
  }

  // Extend the number of inodes managed.
  //
  // It is the caller's responsibility to ensure that there is space
  // on persistent storage for these inodes to be stored.
  zx_status_t Grow(size_t inodes);

 private:
#ifdef __Fuchsia__
  InodeManager(blk_t start_block);
#else
  InodeManager(Bcache* bc, blk_t start_block);
#endif

  blk_t start_block_;
  std::unique_ptr<Allocator> inode_allocator_;
#ifdef __Fuchsia__
  fzl::ResizeableVmoMapper inode_table_;
#else
  Bcache* bc_;
#endif
};

}  // namespace minfs

#endif  // ZIRCON_SYSTEM_ULIB_MINFS_ALLOCATOR_INODE_MANAGER_H_
