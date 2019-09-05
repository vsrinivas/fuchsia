// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>

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
#include <fbl/unique_ptr.h>
#include <fs/transaction/block_transaction.h>
#include <fs/vfs.h>
#include <minfs/writeback.h>

#include "minfs-private.h"

namespace minfs {

#ifdef __Fuchsia__
void WriteTxn::Enqueue(zx_handle_t vmo, blk_t vmo_offset, blk_t dev_offset, blk_t nblocks) {
  ZX_DEBUG_ASSERT(vmo != ZX_HANDLE_INVALID);
  ZX_DEBUG_ASSERT(!IsBuffered());
  ValidateVmoSize(vmo, static_cast<blk_t>(vmo_offset));

  for (auto& request : requests_) {
    if (request.vmo != vmo) {
      continue;
    }

    if (request.vmo_offset == vmo_offset) {
      // Take the longer of the operations (if operating on the same blocks).
      if (nblocks > request.length) {
        block_count_ += nblocks - request.length;
        request.length = nblocks;
      }
      return;
    } else if ((request.vmo_offset + request.length == vmo_offset) &&
               (request.dev_offset + request.length == dev_offset)) {
      // Combine with the previous request, if immediately following.
      request.length += nblocks;
      block_count_ += nblocks;
      return;
    }
  }

  WriteRequest request;
  request.vmo = vmo;
  // NOTE: It's easier to compare everything when dealing
  // with blocks (not offsets!) so the following are described in
  // terms of blocks until we Flush().
  request.vmo_offset = vmo_offset;
  request.dev_offset = dev_offset;
  request.length = nblocks;
  requests_.push_back(std::move(request));

  block_count_ += nblocks;
}

blk_t WriteTxn::BlockStart() const {
  ZX_DEBUG_ASSERT(IsBuffered());
  ZX_DEBUG_ASSERT(requests_.size() > 0);
  return block_start_;
}

void WriteTxn::SetBuffer(fuchsia_hardware_block_VmoID vmoid, blk_t block_start) {
  ZX_DEBUG_ASSERT(vmoid_.id == VMOID_INVALID);
  ZX_DEBUG_ASSERT(vmoid.id != VMOID_INVALID);
  vmoid_ = vmoid;
  block_start_ = block_start;
}

zx_status_t WriteTxn::Transact() {
  // Update all the outgoing transactions to be in disk blocks
  block_fifo_request_t blk_reqs[requests_.size()];
  const uint32_t kDiskBlocksPerMinfsBlock = kMinfsBlockSize / bc_->DeviceBlockSize();
  for (size_t i = 0; i < requests_.size(); i++) {
    blk_reqs[i].group = bc_->BlockGroupID();
    blk_reqs[i].vmoid = vmoid_.id;
    blk_reqs[i].opcode = BLOCKIO_WRITE;
    blk_reqs[i].vmo_offset = requests_[i].vmo_offset * kDiskBlocksPerMinfsBlock;
    blk_reqs[i].dev_offset = requests_[i].dev_offset * kDiskBlocksPerMinfsBlock;
    uint64_t length = requests_[i].length * kDiskBlocksPerMinfsBlock;
    // TODO(ZX-2253): Remove this assertion.
    ZX_ASSERT_MSG(length < UINT32_MAX, "Request size too large");
    blk_reqs[i].length = static_cast<uint32_t>(length);
  }

  // Actually send the operations to the underlying block device.
  zx_status_t status = bc_->Transaction(blk_reqs, requests_.size());

  requests_.reset();
  vmoid_.id = VMOID_INVALID;
  block_count_ = 0;
  return status;
}
#endif

WritebackWork::WritebackWork(Bcache* bc) : WriteTxn(bc), node_count_(0) {}

void WritebackWork::MarkCompleted(zx_status_t status) {
  while (0 < node_count_) {
    vn_[--node_count_] = nullptr;
  }
}

// Allow "pinning" Vnodes so they aren't destroyed while we're completing
// this writeback operation.
void WritebackWork::PinVnode(fbl::RefPtr<VnodeMinfs> vn) {
  for (size_t i = 0; i < node_count_; i++) {
    if (vn_[i].get() == vn.get()) {
      // Already pinned
      return;
    }
  }
  ZX_DEBUG_ASSERT(node_count_ < fbl::count_of(vn_));
  vn_[node_count_++] = std::move(vn);
}

zx_status_t WritebackWork::Complete() {
  zx_status_t status = Transact();
  MarkCompleted(status);
  return status;
}

zx_status_t Transaction::Create(TransactionalFs* minfs, size_t reserve_inodes,
                                size_t reserve_blocks, InodeManager* inode_manager,
                                Allocator* block_allocator, fbl::unique_ptr<Transaction>* out) {
  fbl::unique_ptr<Transaction> transaction(new Transaction(minfs));

  if (reserve_inodes) {
    // The inode allocator is currently not accessed asynchronously.
    // However, acquiring the reservation may cause the superblock to be modified via extension,
    // so we still need to acquire the lock first.
    zx_status_t status =
        inode_manager->Reserve(transaction.get(), reserve_inodes, &transaction->inode_promise_);
    if (status != ZX_OK) {
      return status;
    }
  }

  if (reserve_blocks) {
    zx_status_t status =
        transaction->block_promise_.Initialize(transaction.get(), reserve_blocks, block_allocator);
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
      lock_(minfs->GetLock())
#else
      bc_(minfs->GetMutableBcache())
#endif
{
}

Transaction::~Transaction() {
  // Unreserve all reserved inodes/blocks while the lock is still held.
  inode_promise_.Cancel();
  block_promise_.Cancel();
}

#ifdef __Fuchsia__
void Transaction::EnqueueMetadata(WriteData source, fs::Operation operation) {
  fs::UnbufferedOperation unbuffered_operation = {
    .vmo = zx::unowned_vmo(source),
    .op = operation
  };
  metadata_operations_.Add(std::move(unbuffered_operation));
}

void Transaction::EnqueueData(WriteData source, fs::Operation operation) {
  fs::UnbufferedOperation unbuffered_operation = {
    .vmo = zx::unowned_vmo(source),
    .op = operation
  };
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
void Transaction::EnqueueMetadata(WriteData source, fs::Operation operation) {
  GetMetadataWork()->Enqueue(source, operation.vmo_offset, operation.dev_offset, operation.length);
}

void Transaction::EnqueueData(WriteData source, fs::Operation operation) {
  GetDataWork()->Enqueue(source, operation.vmo_offset, operation.dev_offset, operation.length);
}

void Transaction::PinVnode(fbl::RefPtr<VnodeMinfs> vnode) {
  GetMetadataWork()->PinVnode(std::move(vnode));
}

WritebackWork* Transaction::GetMetadataWork() {
  if (metadata_work_ == nullptr) {
    metadata_work_.reset(new WritebackWork(bc_));
  }

  return metadata_work_.get();
}

WritebackWork* Transaction::GetDataWork() {
  if (data_work_ == nullptr) {
    ZX_DEBUG_ASSERT(metadata_work_ != nullptr);
    data_work_.reset(new WritebackWork(bc_));
  }

  return data_work_.get();
}
#endif

}  // namespace minfs
