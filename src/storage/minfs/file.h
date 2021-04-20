// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MINFS_FILE_H_
#define SRC_STORAGE_MINFS_FILE_H_

#include <functional>
#include <memory>
#include <mutex>

#ifdef __Fuchsia__
#include "src/storage/minfs/vnode_allocation.h"
#endif

#include <lib/zircon-internal/fnv1hash.h>

#include <fbl/algorithm.h>
#include <fbl/ref_ptr.h>

#include "src/lib/storage/vfs/cpp/vnode.h"
#include "src/storage/minfs/format.h"
#include "src/storage/minfs/minfs.h"
#include "src/storage/minfs/superblock.h"
#include "src/storage/minfs/transaction_limits.h"
#include "src/storage/minfs/vnode.h"
#include "src/storage/minfs/writeback.h"

namespace minfs {

// A specialization of the Minfs Vnode which implements a regular file interface.
class File final : public VnodeMinfs, public fbl::Recyclable<File> {
 public:
  explicit File(Minfs* fs);
  ~File() override;

  // fbl::Recyclable interface.
  void fbl_recycle() final { VnodeMinfs::fbl_recycle(); }

 private:
  zx_status_t CanUnlink() const final;

  // minfs::Vnode interface.
  blk_t GetBlockCount() const final;
  uint64_t GetSize() const final;
  void SetSize(uint32_t new_size) final;
  void AcquireWritableBlock(Transaction* transaction, blk_t local_bno, blk_t old_bno,
                            blk_t* out_bno) final;
  void DeleteBlock(PendingWork* transaction, blk_t local_bno, blk_t old_bno, bool indirect) final;
  bool IsDirectory() const final { return false; }
#ifdef __Fuchsia__
  void IssueWriteback(Transaction* transaction, blk_t vmo_offset, blk_t dev_offset,
                      blk_t count) final;
  bool HasPendingAllocation(blk_t vmo_offset) final;
  void CancelPendingWriteback() final;
#endif
  bool DirtyCacheEnabled() const final;

  // fs::Vnode interface.
  fs::VnodeProtocolSet GetProtocols() const final;
  zx_status_t Read(void* data, size_t len, size_t off, size_t* out_actual) final;
  zx_status_t Write(const void* data, size_t len, size_t offset, size_t* out_actual)
      __TA_EXCLUDES(mutex_) final;
  zx_status_t Append(const void* data, size_t len, size_t* out_end, size_t* out_actual) final;
  zx_status_t Truncate(size_t len) final;

  // Updates, in memory, inode's modify_time with current time.
  void UpdateModificationTime();

  // Returns the number of blocks required to persist uncached data of size |length|
  // starting at |offset|.
  zx::status<uint32_t> GetRequiredBlockCount(size_t offset, size_t length);

  // Returns the number of blocks required to persist data of size |length|
  // starting at |offset| with caching enabled.
  zx::status<uint32_t> GetRequiredBlockCountForDirtyCache(size_t offset, size_t length,
                                                          uint32_t uncached_block_count);

  using WalkWriteBlockHandlerType = std::function<zx::status<>(uint32_t, bool, bool)>;
  // Walks all the dirty blocks that need to be written and calls |handler| on
  // each of those blocks.
  zx::status<> WalkFileBlocks(size_t offset, size_t length, WalkWriteBlockHandlerType& handler);

  // Marks blocks of |length| starting at file |offset| as pending.
  zx::status<> MarkRequiredBlocksPending(size_t offset, size_t length);

  bool IsDirty() const final;

  // Returns true if the file dirty cache needs to be flushed. An error here
  // implies that the |length| and |offset| write don't fit in the current
  // filesystem limits - see minfs::GetRequiredBlockCount.
  zx::status<bool> ShouldFlush(bool is_truncate, size_t length, size_t offset);

  // Flushes dirty cache if ShouldFlush returns true.
  zx::status<> CheckAndFlush(bool is_truncate, size_t length, size_t offset);

  // Flush all the pending writes.
  zx::status<> FlushCachedWrites() __TA_EXCLUDES(mutex_) final;

  // Drops all cached writes.
  void DropCachedWrites() final;

  // Flushes(sends the transaction to journaling layer to be written to journal and disk) or caches
  // current transaction.
  zx::status<> FlushTransaction(std::unique_ptr<Transaction> transaction, bool force_flush = false)
      __TA_EXCLUDES(mutex_);

  // Sends the transaction to journaling layer to be written to journal and disk.
  zx::status<> ForceFlushTransaction(std::unique_ptr<Transaction> transaction);

  // Returns a transaction either by converting CachedBlockTransaction to Transaction
  // or by creating a new transaction.
  zx::status<std::unique_ptr<Transaction>> GetTransaction(uint32_t reserve_blocks);
#ifdef __Fuchsia__
  // Allocate all data blocks pending in |allocation_state_|.
  void AllocateAndCommitData(std::unique_ptr<Transaction> transaction);

  // For all data blocks in the range |start| to |start + count|, reserve specific blocks in
  // the allocator to be swapped in at the time the old blocks are swapped out. Metadata blocks
  // are expected to have been allocated previously.
  zx_status_t BlocksSwap(Transaction* state, blk_t start, blk_t count, blk_t* bno);

  // Describes pending allocation data for the vnode. This should only be accessed while a valid
  // Transaction object is held, as it may be modified asynchronously by the DataBlockAssigner
  // thread.
  PendingAllocationData allocation_state_;
#endif

  std::unique_ptr<CachedBlockTransaction> cached_transaction_ __TA_GUARDED(mutex_);
};

}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_FILE_H_
