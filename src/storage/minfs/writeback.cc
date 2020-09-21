// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>

#include <memory>

#ifdef __Fuchsia__
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/zx/vmo.h>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/vector.h>
#endif

#include <utility>

#include <fbl/algorithm.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/macros.h>
#include <fbl/ref_ptr.h>
#include <fs/vfs.h>
#include <minfs/writeback.h>

#include "minfs_private.h"

namespace minfs {

zx_status_t Transaction::Create(TransactionalFs* minfs, size_t reserve_inodes,
                                size_t reserve_blocks, InodeManager* inode_manager,
                                std::unique_ptr<Transaction>* out) {
  auto transaction = std::make_unique<Transaction>(minfs);

  if (reserve_inodes) {
    // The inode allocator is currently not accessed asynchronously.
    // However, acquiring the reservation may cause the superblock to be modified via extension,
    // so we still need to acquire the lock first.
    zx_status_t status =
        inode_manager->Reserve(transaction.get(), reserve_inodes, &transaction->inode_reservation_);
    if (status != ZX_OK) {
      return status;
    }
  }

  if (reserve_blocks) {
    zx_status_t status = transaction->block_reservation_.Reserve(transaction.get(), reserve_blocks);
    if (status != ZX_OK) {
      return status;
    }
  }

  *out = std::move(transaction);
  return ZX_OK;
}

Transaction::Transaction(TransactionalFs* minfs)
    :
#ifdef __Fuchsia__
      lock_(minfs->GetLock()),
#endif
      inode_reservation_(&minfs->GetInodeAllocator()),
      block_reservation_(&minfs->GetBlockAllocator()) {
}

Transaction::~Transaction() {
  // Unreserve all reserved inodes/blocks while the lock is still held.
  inode_reservation_.Cancel();
  block_reservation_.Cancel();
}

#ifdef __Fuchsia__
void Transaction::EnqueueMetadata(storage::Operation operation, storage::BlockBuffer* buffer) {
  storage::UnbufferedOperation unbuffered_operation = {.vmo = zx::unowned_vmo(buffer->Vmo()),
                                                       .op = operation};
  metadata_operations_.Add(std::move(unbuffered_operation));
}

void Transaction::EnqueueData(storage::Operation operation, storage::BlockBuffer* buffer) {
  storage::UnbufferedOperation unbuffered_operation = {.vmo = zx::unowned_vmo(buffer->Vmo()),
                                                       .op = operation};
  data_operations_.Add(std::move(unbuffered_operation));
}

void Transaction::PinVnode(fbl::RefPtr<VnodeMinfs> vnode) {
  for (size_t i = 0; i < pinned_vnodes_.size(); i++) {
    if (pinned_vnodes_[i].get() == vnode.get()) {
      // Already pinned
      return;
    }
  }

  pinned_vnodes_.push_back(std::move(vnode));
}

std::vector<fbl::RefPtr<VnodeMinfs>> Transaction::RemovePinnedVnodes() {
  return std::move(pinned_vnodes_);
}
#else
void Transaction::EnqueueMetadata(storage::Operation operation, storage::BlockBuffer* buffer) {
  builder_.Add(operation, buffer);
}

void Transaction::EnqueueData(storage::Operation operation, storage::BlockBuffer* buffer) {
  builder_.Add(operation, buffer);
}

// No-op - don't need to pin vnodes on host.
void Transaction::PinVnode(fbl::RefPtr<VnodeMinfs> vnode) {}
#endif

}  // namespace minfs
