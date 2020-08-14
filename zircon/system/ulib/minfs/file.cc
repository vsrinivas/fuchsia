// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "file.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <zircon/device/vfs.h>
#include <zircon/time.h>

#include <algorithm>
#include <memory>

#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/string_piece.h>
#include <fs/debug.h>
#include <fs/vfs_types.h>
#include <safemath/checked_math.h>

#ifdef __Fuchsia__
#include <lib/fdio/vfs.h>
#include <lib/fidl-utils/bind.h>
#include <zircon/syscalls.h>

#include <utility>

#include <fbl/auto_lock.h>
#endif

#include "minfs_private.h"
#include "unowned_vmo_buffer.h"
#include "vnode.h"

namespace minfs {

File::File(Minfs* fs) : VnodeMinfs(fs) {}

File::~File() {
#ifdef __Fuchsia__
  ZX_DEBUG_ASSERT_MSG(allocation_state_.GetNodeSize() == inode_.size,
                      "File being destroyed with pending updates to the inode size");
#endif
}

#ifdef __Fuchsia__

// AllocateAndCommitData does the following operations:
//  - Allocates data blocks,
//  - Frees old data blocks (if this were overwritten),
//  - Issues data and metadata writes,
//  - Updates inode to reflect new size and modification time.
//      Writes or fragments of a write may change inode's size, block_count or
//      file block table (dnum, inum, dinum).
void File::AllocateAndCommitData(std::unique_ptr<Transaction> transaction) {
  // Calculate the maximum number of data blocks we can update within one transaction. This is
  // the smallest between half the capacity of the writeback buffer, and the number of direct
  // blocks needed to touch the maximum allowed number of indirect blocks.
  const uint32_t max_direct_blocks =
      kMinfsDirect + (kMinfsDirectPerIndirect * fs_->Limits().GetMaximumMetaDataBlocks());
  const uint32_t max_writeback_blocks = static_cast<blk_t>(fs_->WritebackCapacity() / 2);
  const uint32_t max_blocks = std::min(max_direct_blocks, max_writeback_blocks);

  fbl::Array<blk_t> allocated_blocks(new blk_t[max_blocks], max_blocks);

  // Iterate through all relative block ranges and acquire absolute blocks for each of them.
  while (true) {
    blk_t expected_blocks = allocation_state_.GetTotalPending();
    ZX_ASSERT(expected_blocks <= max_blocks);

    if (expected_blocks == 0) {
      if (inode_.size != allocation_state_.GetNodeSize()) {
        inode_.size = allocation_state_.GetNodeSize();
        ValidateVmoTail(inode_.size);
      }

      // Since we may have pending reservations from an expected update, reset the allocation
      // state. This may happen if the same block range is allocated and de-allocated (e.g.
      // written and truncated) before the state is resolved.
      ZX_ASSERT(allocation_state_.GetNodeSize() == inode_.size);
      allocation_state_.Reset(allocation_state_.GetNodeSize());
      ZX_DEBUG_ASSERT(allocation_state_.IsEmpty());
      break;
    }

    blk_t bno_start, bno_count;
    ZX_ASSERT(allocation_state_.GetNextRange(&bno_start, &bno_count) == ZX_OK);
    ZX_ASSERT(bno_count <= max_blocks);

    // Since we reserved enough space ahead of time, this should not fail.
    ZX_ASSERT(BlocksSwap(transaction.get(), bno_start, bno_count, &allocated_blocks[0]) == ZX_OK);

    // Enqueue each data block one at a time, as they may not be contiguous on disk.
    UnownedVmoBuffer buffer(zx::unowned_vmo(vmo_.get()));
    for (blk_t i = 0; i < bno_count; i++) {
      storage::Operation operation = {
          .type = storage::OperationType::kWrite,
          .vmo_offset = bno_start + i,
          .dev_offset = allocated_blocks[i] + fs_->Info().dat_block,
          .length = 1,
      };
      transaction->EnqueueData(operation, &buffer);
    }

    // Since we are updating the file in "chunks", only update the on-disk inode size
    // with the portion we've written so far.
    blk_t last_byte = (bno_start + bno_count) * fs_->BlockSize();
    ZX_ASSERT(last_byte <= fbl::round_up(allocation_state_.GetNodeSize(), fs_->BlockSize()));

    if (last_byte > inode_.size && last_byte < allocation_state_.GetNodeSize()) {
      // If we have written past the end of the recorded size but have not yet reached the
      // allocated size, update the recorded size to the last byte written.
      inode_.size = last_byte;
    } else if (allocation_state_.GetNodeSize() <= last_byte) {
      // If we have just written to the allocated inode size, update the recorded size
      // accordingly.
      inode_.size = allocation_state_.GetNodeSize();
    }

    ValidateVmoTail(inode_.size);

    // In the future we could resolve on a per state (i.e. reservation) basis, but since swaps are
    // currently only made within a single thread, for now it is okay to resolve everything.
    transaction->PinVnode(fbl::RefPtr(this));
  }

  InodeSync(transaction.get(), kMxFsSyncMtime);
  fs_->CommitTransaction(std::move(transaction));
}

zx_status_t File::BlocksSwap(Transaction* transaction, blk_t start, blk_t count, blk_t* bnos) {
  if (count == 0)
    return ZX_OK;

  VnodeMapper mapper(this);
  VnodeIterator iterator;
  zx_status_t status = iterator.Init(&mapper, transaction, start);
  if (status != ZX_OK)
    return status;

  while (count > 0) {
    const blk_t file_block = static_cast<blk_t>(iterator.file_block());
    ZX_DEBUG_ASSERT(allocation_state_.IsPending(file_block));
    blk_t old_block = iterator.Blk();
    // TODO(fxbug.dev/51587): A value of zero for the block pointer has special meaning: the block
    // is sparse or unmapped. We should add something for this magic constant and fix all places
    // that currently hard code zero.
    if (old_block == 0) {
      inode_.block_count++;
    }
    // For copy-on-write, swap the block out if it's a data block.
    blk_t new_block = old_block;
    fs_->BlockSwap(transaction, old_block, &new_block);
    zx_status_t status = iterator.SetBlk(new_block);
    if (status != ZX_OK)
      return status;
    *bnos++ = new_block;
    bool cleared = allocation_state_.ClearPending(file_block, old_block != 0);
    ZX_DEBUG_ASSERT(cleared);
    --count;
    status = iterator.Advance();
    if (status != ZX_OK)
      return status;
  }
  return iterator.Flush();
}

#endif

blk_t File::GetBlockCount() const {
#ifdef __Fuchsia__
  return inode_.block_count + allocation_state_.GetNewPending();
#else
  return inode_.block_count;
#endif
}

uint64_t File::GetSize() const {
#ifdef __Fuchsia__
  return allocation_state_.GetNodeSize();
#endif
  return inode_.size;
}

void File::SetSize(uint32_t new_size) {
#ifdef __Fuchsia__
  allocation_state_.SetNodeSize(new_size);
#else
  inode_.size = new_size;
#endif
}

void File::AcquireWritableBlock(Transaction* transaction, blk_t local_bno, blk_t old_bno,
                                blk_t* out_bno) {
  bool using_new_block = (old_bno == 0);
#ifdef __Fuchsia__
  allocation_state_.SetPending(local_bno, !using_new_block);
#else
  if (using_new_block) {
    fs_->BlockNew(transaction, out_bno);
    inode_.block_count++;
  } else {
    *out_bno = old_bno;
  }
#endif
}

void File::DeleteBlock(PendingWork* transaction, blk_t local_bno, blk_t old_bno, bool indirect) {
  // If we found a block that was previously allocated, delete it.
  if (old_bno != 0) {
    transaction->DeallocateBlock(old_bno);
    inode_.block_count--;
  }
#ifdef __Fuchsia__
  if (!indirect) {
    // Remove this block from the pending allocation map in case it's set so we do not
    // proceed to allocate a new block.
    allocation_state_.ClearPending(local_bno, old_bno != 0);
  }
#endif
}

#ifdef __Fuchsia__
void File::IssueWriteback(Transaction* transaction, blk_t vmo_offset, blk_t dev_offset,
                          blk_t block_count) {
  // This is a no-op. The blocks are swapped later.
}

bool File::HasPendingAllocation(blk_t vmo_offset) {
  return allocation_state_.IsPending(vmo_offset);
}

void File::CancelPendingWriteback() {
  // Drop all pending writes, revert the size of the inode to the "pre-pending-write" size.
  allocation_state_.Reset(inode_.size);
}

#endif

zx_status_t File::CanUnlink() const { return ZX_OK; }

fs::VnodeProtocolSet File::GetProtocols() const { return fs::VnodeProtocol::kFile; }

zx_status_t File::Read(void* data, size_t len, size_t off, size_t* out_actual) {
  TRACE_DURATION("minfs", "File::Read", "ino", GetIno(), "len", len, "off", off);
  ZX_DEBUG_ASSERT_MSG(FdCount() > 0, "Reading from ino with no fds open");
  FS_TRACE_DEBUG("minfs_read() vn=%p(#%u) len=%zd off=%zd\n", this, GetIno(), len, off);

  fs::Ticker ticker(fs_->StartTicker());
  auto get_metrics = fbl::MakeAutoCall(
      [&ticker, &out_actual, this]() { fs_->UpdateReadMetrics(*out_actual, ticker.End()); });

  Transaction transaction(fs_);
  return ReadInternal(&transaction, data, len, off, out_actual);
}

zx_status_t File::Write(const void* data, size_t len, size_t offset, size_t* out_actual) {
  TRACE_DURATION("minfs", "File::Write", "ino", GetIno(), "len", len, "off", offset);
  ZX_DEBUG_ASSERT_MSG(FdCount() > 0, "Writing to ino with no fds open");
  FS_TRACE_DEBUG("minfs_write() vn=%p(#%u) len=%zd off=%zd\n", this, GetIno(), len, offset);

  *out_actual = 0;
  fs::Ticker ticker(fs_->StartTicker());
  auto get_metrics = fbl::MakeAutoCall(
      [&ticker, &out_actual, this]() { fs_->UpdateWriteMetrics(*out_actual, ticker.End()); });

  // Calculate maximum number of blocks to reserve for this write operation.
  auto reserve_blocks_or = GetRequiredBlockCount(offset, len, fs_->BlockSize());
  if (reserve_blocks_or.is_error()) {
    return reserve_blocks_or.error_value();
  }
  std::unique_ptr<Transaction> transaction;
  zx_status_t status;
  if ((status = fs_->BeginTransaction(0, reserve_blocks_or.value(), &transaction)) != ZX_OK) {
    return status;
  }

  status =
      WriteInternal(transaction.get(), static_cast<const uint8_t*>(data), len, offset, out_actual);
  if (status != ZX_OK) {
    return status;
  }

  // If anything was written, enqueue operations allocated within WriteInternal.
  if (*out_actual != 0) {
    // Ensure this Vnode remains alive while it has an operation in-flight.
    transaction->PinVnode(fbl::RefPtr(this));

#ifdef __Fuchsia__
    AllocateAndCommitData(std::move(transaction));
#else
    InodeSync(transaction.get(), kMxFsSyncMtime);  // Successful writes updates mtime
    fs_->CommitTransaction(std::move(transaction));
#endif
  }

  return ZX_OK;
}

zx_status_t File::Append(const void* data, size_t len, size_t* out_end, size_t* out_actual) {
  zx_status_t status = Write(data, len, GetSize(), out_actual);
  *out_end = GetSize();
  return status;
}

zx_status_t File::Truncate(size_t len) {
  TRACE_DURATION("minfs", "File::Truncate");

  fs::Ticker ticker(fs_->StartTicker());
  auto get_metrics =
      fbl::MakeAutoCall([&ticker, this] { fs_->UpdateTruncateMetrics(ticker.End()); });

  std::unique_ptr<Transaction> transaction;
  // Due to file copy-on-write, up to 1 new (data) block may be required.
  size_t reserve_blocks = 1;
  zx_status_t status;

  if ((status = fs_->BeginTransaction(0, reserve_blocks, &transaction)) != ZX_OK) {
    return status;
  }

  if ((status = TruncateInternal(transaction.get(), len)) != ZX_OK) {
    return status;
  }

#ifdef __Fuchsia__
  // Shortcut case: If we don't have any data blocks to update, we may as well just update
  // the inode by itself.
  //
  // This allows us to avoid "only setting inode_.size" in the data task responsible for
  // calling "AllocateAndCommitData()".
  if (allocation_state_.IsEmpty()) {
    inode_.size = allocation_state_.GetNodeSize();
  }
#endif

  // Sync the inode to persistent storage: although our data blocks will be allocated
  // later, the act of truncating may have allocated indirect blocks.
  //
  // Ensure our inode is consistent with that metadata.
  transaction->PinVnode(fbl::RefPtr(this));
#ifdef __Fuchsia__
  AllocateAndCommitData(std::move(transaction));
#else
  InodeSync(transaction.get(), kMxFsSyncMtime);
  fs_->CommitTransaction(std::move(transaction));
#endif
  return ZX_OK;
}

}  // namespace minfs
