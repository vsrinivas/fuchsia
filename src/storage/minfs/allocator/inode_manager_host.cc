// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include <memory>

#include "src/storage/minfs/allocator/inode_manager.h"
#include "src/storage/minfs/format.h"

namespace minfs {

InodeManager::InodeManager(Bcache* bc, blk_t start_block, uint32_t block_size)
    : start_block_(start_block), block_size_(block_size), bc_(bc) {}

zx::status<std::unique_ptr<InodeManager>> InodeManager::Create(
    Bcache* bc, SuperblockManager* sb, fs::BufferedOperationsBuilder* builder,
    AllocatorMetadata metadata, blk_t start_block, size_t inodes) {
  auto mgr = std::unique_ptr<InodeManager>(new InodeManager(bc, start_block, sb->BlockSize()));
  InodeManager* mgr_raw = mgr.get();

  auto grow_cb = [mgr_raw](uint32_t pool_size) { return mgr_raw->Grow(pool_size); };

  std::unique_ptr<PersistentStorage> storage(new PersistentStorage(
      sb, kMinfsInodeSize, std::move(grow_cb), std::move(metadata), sb->BlockSize()));
  auto inode_allocator_or = Allocator::Create(builder, std::move(storage));
  if (inode_allocator_or.is_error()) {
    return inode_allocator_or.take_error();
  }
  mgr->inode_allocator_ = std::move(inode_allocator_or.value());

  return zx::ok(std::move(mgr));
}

void InodeManager::Update(PendingWork* transaction, ino_t ino, const Inode* inode) {
  // Obtain the offset of the inode within its containing block
  const uint32_t off_of_ino = (ino % kMinfsInodesPerBlock) * kMinfsInodeSize;
  const blk_t inoblock_rel = ino / kMinfsInodesPerBlock;
  const blk_t inoblock_abs = inoblock_rel + start_block_;
  ZX_DEBUG_ASSERT(inoblock_abs < kFVMBlockDataStart);

  // Since host-side tools don't have "mapped vmos", just read / update /
  // write the single absolute inode block.
  uint8_t inodata[BlockSize()];
  (void)bc_->Readblk(inoblock_abs, inodata);
  memcpy(inodata + off_of_ino, inode, kMinfsInodeSize);
  (void)bc_->Writeblk(inoblock_abs, inodata);
}

const Allocator* InodeManager::GetInodeAllocator() const { return inode_allocator_.get(); }

void InodeManager::Load(ino_t ino, Inode* out) const {
  // obtain the block of the inode table we need
  uint32_t off_of_ino = (ino % kMinfsInodesPerBlock) * kMinfsInodeSize;
  uint8_t inodata[BlockSize()];
  (void)bc_->Readblk(start_block_ + (ino / kMinfsInodesPerBlock), inodata);
  const Inode* inode =
      reinterpret_cast<const Inode*>(reinterpret_cast<uintptr_t>(inodata) + off_of_ino);
  memcpy(out, inode, kMinfsInodeSize);
}

zx_status_t InodeManager::Grow(size_t inodes) { return ZX_ERR_NO_SPACE; }

}  // namespace minfs
