// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "inode-manager.h"

#include <stdlib.h>

#include <memory>

namespace minfs {

InodeManager::InodeManager(blk_t start_block) : start_block_(start_block) {}

zx_status_t InodeManager::Create(block_client::BlockDevice* device, SuperblockManager* sb,
                                 fs::ReadTxn* txn, AllocatorMetadata metadata, blk_t start_block,
                                 size_t inodes, std::unique_ptr<InodeManager>* out) {
  auto mgr = std::unique_ptr<InodeManager>(new InodeManager(start_block));
  InodeManager* mgr_raw = mgr.get();

  auto grow_cb = [mgr_raw](uint32_t pool_size) { return mgr_raw->Grow(pool_size); };

  zx_status_t status;
  std::unique_ptr<PersistentStorage> storage(
      new PersistentStorage(device, sb, kMinfsInodeSize, std::move(grow_cb), std::move(metadata)));

  if ((status = Allocator::Create(txn, std::move(storage), &mgr->inode_allocator_)) != ZX_OK) {
    return status;
  }

  uint32_t inoblks =
      (static_cast<uint32_t>(inodes) + kMinfsInodesPerBlock - 1) / kMinfsInodesPerBlock;
  if ((status = mgr->inode_table_.CreateAndMap(inoblks * kMinfsBlockSize, "minfs-inode-table")) !=
      ZX_OK) {
    return status;
  }

  fuchsia_hardware_block_VmoId vmoid;
  status = device->BlockAttachVmo(mgr->inode_table_.vmo(), &vmoid);
  if (status != ZX_OK) {
    return status;
  }
  txn->Enqueue(vmoid.id, 0, start_block, inoblks);

  *out = std::move(mgr);
  return ZX_OK;
}

void InodeManager::Update(PendingWork* transaction, ino_t ino, const Inode* inode) {
  // Obtain the offset of the inode within its containing block
  const uint32_t off_of_ino = (ino % kMinfsInodesPerBlock) * kMinfsInodeSize;
  const blk_t inoblock_rel = ino / kMinfsInodesPerBlock;
  const blk_t inoblock_abs = inoblock_rel + start_block_;
  ZX_DEBUG_ASSERT(inoblock_abs < kFVMBlockDataStart);

  char* inodata = reinterpret_cast<char*>(inode_table_.start()) + inoblock_rel * kMinfsBlockSize;
  memcpy(inodata + off_of_ino, inode, kMinfsInodeSize);

  storage::Operation op = {
      .type = storage::OperationType::kWrite,
      .vmo_offset = inoblock_rel,
      .dev_offset = inoblock_abs,
      .length = 1,
  };
  transaction->EnqueueMetadata(inode_table_.vmo().get(), std::move(op));
}

const Allocator* InodeManager::GetInodeAllocator() const { return inode_allocator_.get(); }

void InodeManager::Load(ino_t ino, Inode* out) const {
  // obtain the block of the inode table we need
  uint32_t off_of_ino = (ino % kMinfsInodesPerBlock) * kMinfsInodeSize;
  const char* inodata = reinterpret_cast<const char*>(inode_table_.start()) +
                        ino / kMinfsInodesPerBlock * kMinfsBlockSize;
  const Inode* inode = reinterpret_cast<const Inode*>(inodata + off_of_ino);
  memcpy(out, inode, kMinfsInodeSize);
}

zx_status_t InodeManager::Grow(size_t inodes) {
  uint32_t inoblks =
      (static_cast<uint32_t>(inodes) + kMinfsInodesPerBlock - 1) / kMinfsInodesPerBlock;
  if (inode_table_.Grow(inoblks * kMinfsBlockSize) != ZX_OK) {
    return ZX_ERR_NO_SPACE;
  }
  return ZX_OK;
}

}  // namespace minfs
