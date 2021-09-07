// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>

#include "src/storage/minfs/minfs_private.h"

#ifndef __Fuchsia__
static_assert(false, "This file is not meant to be used on host");
#endif  // __Fuchsia__

namespace minfs {

bool Minfs::DirtyCacheEnabled() {
#ifdef MINFS_ENABLE_DIRTY_CACHE
  return true;
#else
  return false;
#endif  // MINFS_ENABLE_DIRTY_CACHE
}

[[nodiscard]] bool Minfs::IsJournalErrored() {
  return journal_ != nullptr && !journal_->IsWritebackEnabled();
}

std::vector<fbl::RefPtr<VnodeMinfs>> Minfs::GetDirtyVnodes() {
  fbl::RefPtr<VnodeMinfs> vn;
  std::vector<fbl::RefPtr<VnodeMinfs>> vnodes;

  // vnode_hash_ is locked to release vnode reference. If we are the last one to hold vnode
  // reference and the vnode is clean then this block will end up releasing the vnode. To avoid
  // deadlock, we park clean vnodes in an array which will be released outside of the lock.
  std::vector<fbl::RefPtr<VnodeMinfs>> unused_clean_vnodes;
  if (DirtyCacheEnabled()) {
    // Avoid releasing a reference to |vn| while holding |hash_lock_|.
    fbl::AutoLock lock(&hash_lock_);
    for (auto& raw_vnode : vnode_hash_) {
      vn = fbl::MakeRefPtrUpgradeFromRaw(&raw_vnode, hash_lock_);
      if (vn == nullptr) {
        continue;
      }
      if (vn->IsDirty()) {
        vnodes.push_back(std::move(vn));
      } else {
        unused_clean_vnodes.push_back(std::move(vn));
      }
    }
  }
  return vnodes;
}

zx_status_t Minfs::ContinueTransaction(size_t reserve_blocks,
                                       std::unique_ptr<CachedBlockTransaction> cached_transaction,
                                       std::unique_ptr<Transaction>* out) {
  ZX_ASSERT(DirtyCacheEnabled());
  if (journal_ == nullptr) {
    return ZX_ERR_BAD_STATE;
  }

  if (!journal_->IsWritebackEnabled()) {
    return ZX_ERR_IO_REFUSED;
  }

  // TODO(unknown): Once we are splitting up write
  // transactions, assert this on host as well.
  ZX_DEBUG_ASSERT(reserve_blocks <= limits_.GetMaximumDataBlocks());

  *out = Transaction::FromCachedBlockTransaction(this, std::move(cached_transaction));
  // Reserve blocks from allocators before returning
  // WritebackWork to client.
  return (*out)->ExtendBlockReservation(reserve_blocks);
}

zx::status<> Minfs::AddDirtyBytes(uint64_t dirty_bytes, bool allocated) {
  if (!DirtyCacheEnabled()) {
    return zx::ok();
  }

  if (!allocated) {
    fbl::AutoLock lock(&hash_lock_);
    // We need to allocate the block. Make sure that we have
    // enough space.
    uint32_t blocks_needed = BlocksReserved();
    uint32_t local_blocks_available = Info().block_count - Info().alloc_block_count;
    if (blocks_needed > local_blocks_available) {
      // Check if fvm has free slices.
      fuchsia_hardware_block_volume_VolumeInfo fvm_info;
      if (FVMQuery(&fvm_info) != ZX_OK) {
        FX_LOGS_FIRST_N(WARNING, 10)
            << "Minfs::AddDirtyBytes can't call FvmQuery, assuming no space.";
        return zx::error(ZX_ERR_NO_SPACE);
      }
      uint64_t free_slices = fvm_info.pslice_total_count - fvm_info.pslice_allocated_count;
      uint64_t blocks_available =
          local_blocks_available + (fvm_info.slice_size * free_slices / Info().BlockSize());
      if (blocks_needed > blocks_available) {
        FX_LOGS_FIRST_N(WARNING, 10) << "Minfs::AddDirtyBytes can't find any free blocks.";
        return zx::error(ZX_ERR_NO_SPACE);
      }
    }
  }
  metrics_.dirty_bytes += dirty_bytes;

  return zx::ok();
}

void Minfs::SubtractDirtyBytes(uint64_t dirty_bytes, bool allocated) {
  if (!DirtyCacheEnabled()) {
    return;
  }

  ZX_ASSERT(dirty_bytes <= metrics_.dirty_bytes.load());
  metrics_.dirty_bytes -= dirty_bytes;
}

}  // namespace minfs
