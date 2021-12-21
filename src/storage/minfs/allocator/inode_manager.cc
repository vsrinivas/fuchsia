// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/allocator/inode_manager.h"

#include <lib/syslog/cpp/macros.h>
#include <stdlib.h>

#include <memory>

#include <storage/buffer/block_buffer.h>

#include "src/storage/minfs/format.h"
#include "src/storage/minfs/unowned_vmo_buffer.h"

namespace minfs {

InodeManager::InodeManager(blk_t start_block, uint32_t block_size)
    : start_block_(start_block), block_size_(block_size) {}

zx::status<std::unique_ptr<InodeManager>> InodeManager::Create(
    block_client::BlockDevice* device, SuperblockManager* sb,
    fs::BufferedOperationsBuilder* builder, AllocatorMetadata metadata, blk_t start_block,
    size_t inodes) {
  auto mgr = std::unique_ptr<InodeManager>(new InodeManager(start_block, sb->BlockSize()));
  InodeManager* mgr_raw = mgr.get();

  auto grow_cb = [mgr_raw](uint32_t pool_size) { return mgr_raw->Grow(pool_size); };

  std::unique_ptr<PersistentStorage> storage(new PersistentStorage(
      device, sb, kMinfsInodeSize, std::move(grow_cb), std::move(metadata), sb->BlockSize()));
  auto inode_allocator_or = Allocator::Create(builder, std::move(storage));
  if (inode_allocator_or.is_error()) {
    return inode_allocator_or.take_error();
  }
  mgr->inode_allocator_ = std::move(inode_allocator_or.value());

  uint32_t inoblks =
      (static_cast<uint32_t>(inodes) + kMinfsInodesPerBlock - 1) / kMinfsInodesPerBlock;
  if (zx_status_t status =
          mgr->inode_table_.CreateAndMap(inoblks * sb->BlockSize(), "minfs-inode-table");
      status != ZX_OK) {
    return zx::error(status);
  }

  storage::Vmoid vmoid;
  if (zx_status_t status = device->BlockAttachVmo(mgr->inode_table_.vmo(), &vmoid);
      status != ZX_OK) {
    return zx::error(status);
  }
  vmoid_t id = vmoid.get();
  builder->AddVmoid(storage::OwnedVmoid(std::move(vmoid), device));

  storage::Operation operation{
      .type = storage::OperationType::kRead,
      .vmo_offset = 0,
      .dev_offset = start_block,
      .length = inoblks,
  };

  fs::internal::BorrowedBuffer buffer(id);
  builder->Add(operation, &buffer);

  return zx::ok(std::move(mgr));
}

void InodeManager::Update(PendingWork* transaction, ino_t ino, const Inode* inode) {
  // Obtain the offset of the inode within its containing block.
  const uint32_t off_of_ino = (ino % kMinfsInodesPerBlock) * kMinfsInodeSize;
  const blk_t inoblock_rel = ino / kMinfsInodesPerBlock;
  const blk_t inoblock_abs = inoblock_rel + start_block_;
  ZX_DEBUG_ASSERT(inoblock_abs < kFVMBlockDataStart);

  char* inodata = reinterpret_cast<char*>(inode_table_.start()) + inoblock_rel * BlockSize();
  memcpy(inodata + off_of_ino, inode, kMinfsInodeSize);

  storage::Operation operation = {
      .type = storage::OperationType::kWrite,
      .vmo_offset = inoblock_rel,
      .dev_offset = inoblock_abs,
      .length = 1,
  };
  UnownedVmoBuffer buffer(zx::unowned_vmo(inode_table_.vmo()));
  transaction->EnqueueMetadata(operation, &buffer);
}

const Allocator* InodeManager::GetInodeAllocator() const { return inode_allocator_.get(); }

void InodeManager::Load(ino_t ino, Inode* out) const {
  // Obtain the block of the inode table we need.
  uint32_t off_of_ino = (ino % kMinfsInodesPerBlock) * kMinfsInodeSize;
  const char* inodata = reinterpret_cast<const char*>(inode_table_.start()) +
                        ino / kMinfsInodesPerBlock * BlockSize();
  const Inode* inode = reinterpret_cast<const Inode*>(inodata + off_of_ino);
  memcpy(out, inode, kMinfsInodeSize);
}

zx_status_t InodeManager::Grow(size_t inodes) {
  uint32_t inoblks =
      (static_cast<uint32_t>(inodes) + kMinfsInodesPerBlock - 1) / kMinfsInodesPerBlock;
  if (zx_status_t status = inode_table_.Grow(inoblks * BlockSize()); status != ZX_OK) {
    FX_LOGS(WARNING) << "InodeManager::Grow: failed: " << status;
    return ZX_ERR_NO_SPACE;
  }
  return ZX_OK;
}

}  // namespace minfs
