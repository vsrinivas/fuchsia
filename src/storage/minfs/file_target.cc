// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains Fuchsia specific minfs::File code.

#include <lib/syslog/cpp/macros.h>

#include "src/lib/storage/vfs/cpp/trace.h"
#include "src/storage/minfs/file.h"
#include "src/storage/minfs/minfs_private.h"
#include "zircon/assert.h"

namespace minfs {

// If number of dirty data blocks cross this limit, the file dirty cache is
// force flushed. The upper limit for this constant is set by the largest
// transaction minfs/journal allows. This is currently set to number of
// data blocks that can in a transaction if each data block needs to allocate
// a indirect/double indirect block.
// This is slightly a conservative limit and we can increase it by improving
// calculations in GetRequiredBlockCountForDirtyCache.
// See also: minfs::TransactionLimits and
// File::GetRequiredBlockCountForDirtyCache.
constexpr uint32_t kDirtyBlocksPerFile = 256;

File::~File() {
  ZX_ASSERT_MSG(allocation_state_.GetTotalPending() == 0 || Vfs()->IsJournalErrored(),
                "File was found dirty");
  DropCachedWrites();
  ZX_ASSERT_MSG(allocation_state_.GetNodeSize() == GetInode()->size || Vfs()->IsJournalErrored(),
                "File being destroyed with pending updates to the inode size");
}

bool File::DirtyCacheEnabled() const { return Minfs::DirtyCacheEnabled(); }

bool File::IsDirty() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return cached_transaction_ != nullptr;
}

zx::status<> File::WalkFileBlocks(size_t offset, size_t length,
                                  WalkWriteBlockHandlerType& handler) {
  ZX_ASSERT(DirtyCacheEnabled());
  blk_t start_block = offset / Vfs()->BlockSize();
  blk_t end_block = (offset + length + Vfs()->BlockSize() - 1) / Vfs()->BlockSize();
  size_t aligned_length = (end_block - start_block) * Vfs()->BlockSize();
  while (aligned_length > 0) {
    VnodeMapper mapper(this);
    VnodeIterator iterator;
    if (auto status = iterator.Init(&mapper, nullptr, start_block); status != ZX_OK) {
      return zx::error(status);
    }

    bool allocated = !(iterator.Blk() == 0);
    bool is_pending = allocation_state_.IsPending(start_block);

    if (auto status = handler(start_block, allocated, is_pending); status.is_error()) {
      return status;
    }
    aligned_length -= Vfs()->BlockSize();
    start_block++;
  }
  return zx::ok();
}

zx::status<uint32_t> File::GetRequiredBlockCountForDirtyCache(size_t offset, size_t length,
                                                              uint32_t uncached_block_count) {
  ZX_ASSERT(DirtyCacheEnabled());
  size_t data_blocks_to_write = 0;
  WalkWriteBlockHandlerType count_blocks = [&data_blocks_to_write, &uncached_block_count](
                                               uint32_t block, bool allocated,
                                               bool is_pending) -> zx::status<> {
    if (!is_pending) {
      data_blocks_to_write++;
    } else {
      uncached_block_count--;
    }
    return zx::ok();
  };
  if (auto status = WalkFileBlocks(offset, length, count_blocks); status.is_error()) {
    return zx::error(status.status_value());
  }
  if (data_blocks_to_write == 0) {
    uncached_block_count = 0;
  }
  return zx::ok(uncached_block_count);
}

zx::status<> File::MarkRequiredBlocksPending(size_t offset, size_t length) {
  ZX_ASSERT(DirtyCacheEnabled());
  WalkWriteBlockHandlerType mark_pending = [this](uint32_t block, bool allocated,
                                                  bool is_pending) -> zx::status<> {
    if (!is_pending) {
      allocation_state_.SetPending(block, allocated);
      auto status = Vfs()->AddDirtyBytes(Vfs()->BlockSize(), allocated);
      if (status.is_error()) {
        return zx::error(status.error_value());
      }
    }
    return zx::ok();
  };
  if (auto status = WalkFileBlocks(offset, length, mark_pending); status.is_error()) {
    return zx::error(status.error_value());
  }
  return zx::ok();
}

void File::DropCachedWrites() {
  if (!DirtyCacheEnabled()) {
    return;
  }
  uint32_t block_count = 0;
  WalkWriteBlockHandlerType clear_pending = [this, &block_count](uint32_t block, bool allocated,
                                                                 bool is_pending) -> zx::status<> {
    if (!is_pending) {
      return zx::ok();
    }
    allocation_state_.ClearPending(block, allocated);
    Vfs()->SubtractDirtyBytes(Vfs()->BlockSize(), allocated);
    block_count++;
    return zx::ok();
  };

  bool result = WalkFileBlocks(0, GetSize(), clear_pending).is_ok();

  // We should never fail to clear pending writes.
  ZX_ASSERT(result);

  // Unless the file is not unlinked or the filesystem is in errored state, we should not be
  // dropping dirty cache of the file.
  ZX_ASSERT(block_count == 0 || IsUnlinked() || Vfs()->IsJournalErrored());

  // At the end of this function, number of pending blocks should drop to zero.
  ZX_ASSERT(allocation_state_.GetTotalPending() == 0);
}

zx::status<> File::FlushCachedWrites() {
  if (!DirtyCacheEnabled()) {
    std::lock_guard<std::mutex> lock(mutex_);
    ZX_DEBUG_ASSERT(cached_transaction_ == nullptr);
    return zx::ok();
  }

  std::unique_ptr<CachedBlockTransaction> cached_transaction;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    cached_transaction = std::move(cached_transaction_);
  }
  if (cached_transaction == nullptr) {
    if (Vfs()->IsJournalErrored()) {
      // Here we want to drop cached writes (the reservations and pending bits) if the journal is
      // errored. If journal is in a healthy state, we return success because journalled metadata
      // write might still be working on the file.
      DropCachedWrites();
    }
    return zx::ok();
  }

  std::unique_ptr<Transaction> transaction;
  if (auto status = Vfs()->ContinueTransaction(0, std::move(cached_transaction), &transaction);
      status != ZX_OK) {
    return zx::error(status);
  }

  return ForceFlushTransaction(std::move(transaction));
}

zx::status<bool> File::ShouldFlush(bool is_truncate, size_t length, size_t offset) {
  if (!DirtyCacheEnabled()) {
    std::lock_guard<std::mutex> lock(mutex_);
    ZX_DEBUG_ASSERT(cached_transaction_ == nullptr);
    return zx::ok(true);
  }

  if (is_truncate) {
    FX_LOGS(DEBUG) << "Flush from truncate";
    return zx::ok(true);
  }

  // Calculate maximum number of blocks to reserve for this write operation.
  // If we need more blocks to write than available, maybe flushing pending writes might help
  // some of the blocks reserved for copy-on-write.
  auto reserve_blocks_or = GetRequiredBlockCount(offset, length);
  if (reserve_blocks_or.is_error()) {
    return zx::error(reserve_blocks_or.error_value());
  }

  size_t reserve_blocks = reserve_blocks_or.value();
  return zx::ok((allocation_state_.GetTotalPending() >= kDirtyBlocksPerFile ||
                 Vfs()->BlocksAvailable() < reserve_blocks));
}

zx::status<> File::ForceFlushTransaction(std::unique_ptr<Transaction> transaction) {
  // Ensure this Vnode remains alive while it has an operation in-flight.
  transaction->PinVnode(fbl::RefPtr(this));
  AllocateAndCommitData(std::move(transaction));
  return zx::ok();
}

zx::status<> File::FlushTransaction(std::unique_ptr<Transaction> transaction, bool force_flush) {
  if (!DirtyCacheEnabled() || force_flush) {
    // Shortcut case: If we don't have any data blocks to update, we may as well just update
    // the inode by itself.
    //
    // This allows us to avoid "only setting GetInode()->size" in the data task responsible for
    // calling "AllocateAndCommitData()".
    if (allocation_state_.IsEmpty()) {
      GetMutableInode()->size = allocation_state_.GetNodeSize();
    }
    return ForceFlushTransaction(std::move(transaction));
  }

  GetMutableInode()->size = allocation_state_.GetNodeSize();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ZX_ASSERT(cached_transaction_ == nullptr);
    cached_transaction_ = std::make_unique<CachedBlockTransaction>(
        Transaction::TakeBlockReservations(std::move(transaction)));
  }

  // With this write, we may have crossed our caching limit. If so flush the write(s).
  auto flush_or = ShouldFlush(false, 0, 0);
  if (flush_or.is_error()) {
    return zx::error(flush_or.error_value());
  }

  if (flush_or.value() || force_flush) {
    return FlushCachedWrites();
  }
  return zx::ok();
}

}  // namespace minfs
