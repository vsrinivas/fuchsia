// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file describes the structure used to access inodes.
// Currently, this structure is implemented on-disk as a table.

#ifndef SRC_STORAGE_MINFS_ALLOCATOR_INODE_MANAGER_H_
#define SRC_STORAGE_MINFS_ALLOCATOR_INODE_MANAGER_H_

#include <cstdio>
#include <memory>

#include <fbl/macros.h>

#include "src/storage/minfs/format.h"

#ifdef __Fuchsia__
#include <lib/fzl/resizeable-vmo-mapper.h>

#include <block-client/cpp/block-device.h>
#endif

#include "src/storage/minfs/allocator/allocator.h"

namespace minfs {

class InspectableInodeManager {
 public:
  virtual ~InspectableInodeManager() = default;

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
  // Not copyable or movable
  InodeManager(const InodeManager&) = delete;
  InodeManager& operator=(const InodeManager&) = delete;
  InodeManager(InodeManager&&) = delete;
  InodeManager& operator=(InodeManager&&) = delete;

  ~InodeManager() override = default;

#ifdef __Fuchsia__
  static zx::status<std::unique_ptr<InodeManager>> Create(block_client::BlockDevice* device,
                                                          SuperblockManager* sb,
                                                          fs::BufferedOperationsBuilder* builder,
                                                          AllocatorMetadata metadata,
                                                          blk_t start_block, size_t inodes);
#else
  static zx::status<std::unique_ptr<InodeManager>> Create(Bcache* bc, SuperblockManager* sb,
                                                          fs::BufferedOperationsBuilder* builder,
                                                          AllocatorMetadata metadata,
                                                          blk_t start_block, size_t inodes);
#endif

  // Reserve |inodes| inodes in the allocator.
  static zx::status<> Reserve(PendingWork* transaction, size_t inodes,
                              AllocatorReservation* reservation) {
    return reservation->Reserve(transaction, inodes);
  }

  // Free an inode.
  void Free(Transaction* transaction, size_t index) {
    inode_allocator_->Free(&transaction->inode_reservation(), index);
  }

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

  Allocator& inode_allocator() { return *inode_allocator_; }

 private:
#ifdef __Fuchsia__
  explicit InodeManager(blk_t start_block, uint32_t block_size);
#else
  InodeManager(Bcache* bc, blk_t start_block, uint32_t block_size);
#endif

  uint32_t BlockSize() const {
    // Either intentionally or unintenttionally, we do not want to change block
    // size to anything other than kMinfsBlockSize yet. This is because changing
    // block size might lead to format change and also because anything other
    // than 8k is not well tested. So assert when we find block size other
    // than 8k.
    ZX_ASSERT(block_size_ == kMinfsBlockSize);
    return block_size_;
  }

  blk_t start_block_;

  // Filesystem block size.
  uint32_t block_size_ = {};
  std::unique_ptr<Allocator> inode_allocator_;
#ifdef __Fuchsia__
  fzl::ResizeableVmoMapper inode_table_;
#else
  Bcache* bc_;
#endif
};

}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_ALLOCATOR_INODE_MANAGER_H_
