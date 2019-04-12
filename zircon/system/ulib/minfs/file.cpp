// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/string_piece.h>
#include <fs/block-txn.h>
#include <safemath/checked_math.h>
#include <zircon/device/vfs.h>
#include <zircon/time.h>

#ifdef __Fuchsia__
#include <lib/fdio/vfs.h>
#include <lib/fidl-utils/bind.h>
#include <fbl/auto_lock.h>
#include <zircon/syscalls.h>

#include <utility>
#endif

#include "file.h"
#include "minfs-private.h"
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

void File::AllocateData() {
    // Calculate the maximum number of data blocks we can update within one transaction. This is
    // the smallest between half the capacity of the writeback buffer, and the number of direct
    // blocks needed to touch the maximum allowed number of indirect blocks.
    const uint32_t max_direct_blocks =
        kMinfsDirect + (kMinfsDirectPerIndirect * fs_->Limits().GetMaximumMetaDataBlocks());
    const uint32_t max_writeback_blocks = static_cast<blk_t>(fs_->WritebackCapacity() / 2);
    const uint32_t max_blocks = fbl::min(max_direct_blocks, max_writeback_blocks);

    fbl::Array<blk_t> allocated_blocks(new blk_t[max_blocks], max_blocks);

    // Iterate through all relative block ranges and acquire absolute blocks for each of them.
    while (true) {
        fbl::unique_ptr<Transaction> transaction;
        ZX_ASSERT(fs_->BeginTransaction(0, 0, &transaction) == ZX_OK);

        blk_t expected_blocks = allocation_state_.GetTotalPending();

        if (expected_blocks == 0) {
            if (inode_.size != allocation_state_.GetNodeSize()) {
                inode_.size = allocation_state_.GetNodeSize();
                ValidateVmoTail(inode_.size);
                InodeSync(transaction->GetWork(), kMxFsSyncMtime);
                __UNUSED zx_status_t status = fs_->CommitTransaction(std::move(transaction));
            }

            // Since we may have pending reservations from an expected update, reset the allocation
            // state. This may happen if the same block range is allocated and de-allocated (e.g.
            // written and truncated) before the state is resolved.
            ZX_ASSERT(allocation_state_.GetNodeSize() == inode_.size);
            allocation_state_.Reset(allocation_state_.GetNodeSize());
            ZX_DEBUG_ASSERT(allocation_state_.IsEmpty());

            // Stop processing if we have not found any data blocks to update.
            break;
        }

        blk_t bno_start, bno_count;
        ZX_ASSERT(allocation_state_.GetNextRange(&bno_start, &bno_count) == ZX_OK);

        // Transfer reserved blocks from the vnode's allocation state to the current Transaction.
        transaction->MergeBlockPromise(allocation_state_.GetPromise());

        // Write to data blocks must be done in a separate transaction from the metadata updates to
        // ensure that all user data goes out to disk before associated metadata.
        transaction->InitDataWork();

        if (bno_start + bno_count >= kMinfsDirect) {
            // Calculate the number of pre-indirect blocks. These will not factor into the number
            // of indirect blocks being touched, and can be added back at the end.
            blk_t pre_indirect = bno_start < kMinfsDirect ? kMinfsDirect - bno_start : 0;

            // First direct block managed by an indirect block.
            blk_t indirect_start = bno_start - fbl::min(bno_start, kMinfsDirect);

            // Index of that direct block within the indirect block.
            blk_t indirect_index = indirect_start % kMinfsDirectPerIndirect;

            // The maximum number of direct blocks that can be updated without touching beyond the
            // maximum indirect blocks. This includes any direct blocks prior to the indirect
            // section.
            blk_t relative_direct_max = max_direct_blocks - kMinfsDirect - indirect_index
                                        + pre_indirect;

            // Determine actual max count between the indirect and writeback constraints.
            blk_t max_count = fbl::min(relative_direct_max, max_writeback_blocks);

            // Subtract direct blocks contained within the same indirect block before our starting
            // point to ensure that we do not go beyond the maximum number of indirect blocks.
            bno_count = fbl::min(bno_count, max_count);
        }

        ZX_ASSERT(bno_count <= max_blocks);

        // Since we reserved enough space ahead of time, this should not fail.
        ZX_ASSERT(BlocksSwap(transaction.get(), bno_start, bno_count, &allocated_blocks[0]) ==
                  ZX_OK);

        // Enqueue each data block one at a time, as they may not be contiguous on disk.
        for (blk_t i = 0; i < bno_count; i++) {
            transaction->GetDataWork()->Enqueue(vmo_.get(), bno_start + i,
                                                allocated_blocks[i] + fs_->Info().dat_block, 1);
        }

        transaction->GetDataWork()->PinVnode(fbl::WrapRefPtr(this));
        // Enqueue may fail if we are in a readonly state, but we should continue resolving all
        // pending allocations.
        __UNUSED zx_status_t status = fs_->EnqueueWork(transaction->RemoveDataWork());

        // Since we are updating the file in "chunks", only update the on-disk inode size
        // with the portion we've written so far.
        blk_t last_byte = (bno_start + bno_count) * kMinfsBlockSize;
        ZX_ASSERT(last_byte <= fbl::round_up(allocation_state_.GetNodeSize(), kMinfsBlockSize));

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
        InodeSync(transaction->GetWork(), kMxFsSyncMtime);

        // In the future we could resolve on a per state (i.e. promise) basis, but since swaps are
        // currently only made within a single thread, for now it is okay to resolve everything.
        transaction->GetWork()->PinVnode(fbl::WrapRefPtr(this));
        transaction->Resolve();

        // Return remaining reserved blocks back to the allocation state.
        blk_t bno_remaining = expected_blocks - bno_count;
        transaction->GiveBlocksToPromise(bno_remaining, allocation_state_.GetPromise());

        // Commit may fail if we are in a readonly state, but we should continue resolving all
        // pending allocations.
        status = fs_->CommitTransaction(std::move(transaction));
    }
}

zx_status_t File::BlocksSwap(Transaction* transaction, blk_t start, blk_t count, blk_t* bnos) {
    auto block_callback = [this, transaction](blk_t local_bno, blk_t old_bno, blk_t* out_bno) {
        ZX_DEBUG_ASSERT(allocation_state_.IsPending(local_bno));
        if (old_bno == 0) {
            inode_.block_count++;
        }
        // For copy-on-write, swap the block out if it's a data block.
        fs_->BlockSwap(transaction, old_bno, out_bno);
        bool cleared = allocation_state_.ClearPending(local_bno, old_bno != 0);
        ZX_DEBUG_ASSERT(cleared);
    };

    BlockOpArgs op_args(transaction, BlockOp::kSwap, std::move(block_callback), start, count, bnos);
    return ApplyOperation(&op_args);
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

void File::DeleteBlock(Transaction* transaction, blk_t local_bno, blk_t old_bno) {
    // If we found a block that was previously allocated, delete it.
    if (old_bno != 0) {
        fs_->BlockFree(transaction, old_bno);
        inode_.block_count--;
    }
#ifdef __Fuchsia__
    // Remove this block from the pending allocation map in case it's set so we do not
    // proceed to allocate a new block.
    allocation_state_.ClearPending(local_bno, old_bno != 0);
#endif
}

#ifdef __Fuchsia__
void File::IssueWriteback(Transaction* transaction, blk_t vmo_offset, blk_t dev_offset,
                          blk_t block_count) {
    ZX_ASSERT(transaction != nullptr);
    AllocatorPromise block_promise;
    transaction->GiveBlocksToPromise(block_count, &block_promise);
    block_promise.GiveBlocks(block_count, allocation_state_.GetPromise());
}

bool File::HasPendingAllocation(blk_t vmo_offset) {
    return allocation_state_.IsPending(vmo_offset);
}

void File::CancelPendingWriteback() {
    // Drop all pending writes, revert the size of the inode to the "pre-pending-write" size.
    allocation_state_.Reset(inode_.size);
}

#endif

zx_status_t File::CanUnlink() const {
    return ZX_OK;
}

zx_status_t File::ValidateFlags(uint32_t flags) {
    FS_TRACE_DEBUG("File::ValidateFlags(0x%x) vn=%p(#%u)\n", flags, this, GetIno());
    if (flags & ZX_FS_FLAG_DIRECTORY) {
        return ZX_ERR_NOT_DIR;
    }
    return ZX_OK;
}

zx_status_t File::Read(void* data, size_t len, size_t off, size_t* out_actual) {
    TRACE_DURATION("minfs", "File::Read", "ino", GetIno(), "len", len, "off", off);
    ZX_DEBUG_ASSERT_MSG(FdCount() > 0, "Reading from ino with no fds open");
    FS_TRACE_DEBUG("minfs_read() vn=%p(#%u) len=%zd off=%zd\n", this, GetIno(), len, off);

    fs::Ticker ticker(fs_->StartTicker());
    auto get_metrics = fbl::MakeAutoCall([&ticker, &out_actual, this]() {
        fs_->UpdateReadMetrics(*out_actual, ticker.End());
    });

    Transaction transaction(fs_);
    return ReadInternal(&transaction, data, len, off, out_actual);
}

zx_status_t File::Write(const void* data, size_t len, size_t offset,
                              size_t* out_actual) {
    TRACE_DURATION("minfs", "File::Write", "ino", GetIno(), "len", len, "off", offset);
    ZX_DEBUG_ASSERT_MSG(FdCount() > 0, "Writing to ino with no fds open");
    FS_TRACE_DEBUG("minfs_write() vn=%p(#%u) len=%zd off=%zd\n", this, GetIno(), len, offset);

    *out_actual = 0;
    fs::Ticker ticker(fs_->StartTicker());
    auto get_metrics = fbl::MakeAutoCall([&ticker, &out_actual, this]() {
        fs_->UpdateWriteMetrics(*out_actual, ticker.End());
    });

    blk_t reserve_blocks;
    // Calculate maximum number of blocks to reserve for this write operation.
    zx_status_t status = GetRequiredBlockCount(offset, len, &reserve_blocks);
    if (status != ZX_OK) {
        return status;
    }
    fbl::unique_ptr<Transaction> transaction;
    if ((status = fs_->BeginTransaction(0, reserve_blocks, &transaction)) != ZX_OK) {
        return status;
    }

    status = WriteInternal(transaction.get(), data, len, offset, out_actual);
    if (status != ZX_OK) {
        return status;
    }
    if (*out_actual != 0) {
        // Enqueue metadata allocated via write.
        InodeSync(transaction->GetWork(), kMxFsSyncMtime);  // Successful writes updates mtime
        transaction->GetWork()->PinVnode(fbl::WrapRefPtr(this));
        status = fs_->CommitTransaction(std::move(transaction));

#ifdef __Fuchsia__
        // Enqueue data allocated via write.
        fs_->EnqueueDataTask([file = fbl::WrapRefPtr(this)](TransactionalFs*) mutable {
            file->AllocateData();
        });
#endif
    }

    return status;
}

zx_status_t File::Append(const void* data, size_t len, size_t* out_end,
                               size_t* out_actual) {
    zx_status_t status = Write(data, len, GetSize(), out_actual);
    *out_end = GetSize();
    return status;
}

zx_status_t File::Truncate(size_t len) {
    TRACE_DURATION("minfs", "File::Truncate");

    fs::Ticker ticker(fs_->StartTicker());
    auto get_metrics = fbl::MakeAutoCall([&ticker, this] {
        fs_->UpdateTruncateMetrics(ticker.End());
    });

    fbl::unique_ptr<Transaction> transaction;
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
    // calling "AllocateData()".
    if (allocation_state_.IsEmpty()) {
        inode_.size = allocation_state_.GetNodeSize();
    }
#endif

    // Sync the inode to persistent storage: although our data blocks will be allocated
    // later, the act of truncating may have allocated indirect blocks.
    //
    // Ensure our inode is consistent with that metadata.
    InodeSync(transaction->GetWork(), kMxFsSyncMtime);
    transaction->GetWork()->PinVnode(fbl::WrapRefPtr(this));
    status = fs_->CommitTransaction(std::move(transaction));
#ifdef __Fuchsia__
    // Enqueue data allocated via write.
    if (len != inode_.size) {
        fs_->EnqueueDataTask([file = fbl::WrapRefPtr(this)](TransactionalFs*) mutable {
            file->AllocateData();
        });
    }
#endif
    return status;
}

} // namespace minfs
