// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MINFS_WRITEBACK_H_
#define SRC_STORAGE_MINFS_WRITEBACK_H_

#include <lib/zx/status.h>

#include <memory>
#include <utility>
#include <vector>

#include <fbl/ref_ptr.h>

#ifdef __Fuchsia__
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/zx/vmo.h>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fs/transaction/writeback.h>
#endif

#include <fbl/algorithm.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/macros.h>
#include <fs/queue.h>
#include <fs/transaction/buffered_operations_builder.h>
#include <fs/vfs.h>

#include "src/storage/minfs/allocator_reservation.h"
#include "src/storage/minfs/bcache.h"
#include "src/storage/minfs/cached_block_transaction.h"
#include "src/storage/minfs/format.h"
#include "src/storage/minfs/pending_work.h"

namespace minfs {

class DataAssignableVnode;
class InodeManager;
class TransactionalFs;
class VnodeMinfs;

// Tracks the current transaction, including any enqueued writes, and reserved blocks
// and inodes. Also handles allocation of previously reserved blocks/inodes.
// Upon construction, acquires a lock to ensure that all work being done within the
// scope of the transaction is thread-safe. Specifically, the Minfs superblock, block bitmap, and
// inode table, as well as the Vnode block count and inode size may in the near future be modified
// asynchronously. Since these modifications require a Transaction to be in progress, this lock
// will protect against multiple simultaneous writes to these structures.
class Transaction final : public PendingWork {
 public:
  static zx_status_t Create(TransactionalFs* minfs, size_t reserve_inodes, size_t reserve_blocks,
                            InodeManager* inode_manager, std::unique_ptr<Transaction>* out);

  // Creates a Transaction object from CachedBlockTransaction. Consumes cached_transaction.
  static std::unique_ptr<Transaction> FromCachedBlockTransaction(
      TransactionalFs* minfs, std::unique_ptr<CachedBlockTransaction> cached_transaction);

  Transaction() = delete;

  explicit Transaction(TransactionalFs* minfs,
                       std::unique_ptr<CachedBlockTransaction> cached_transaction = nullptr);

  ~Transaction() final;

  AllocatorReservation& inode_reservation() { return inode_reservation_; }
  AllocatorReservation& block_reservation() { return *block_reservation_; }

  ////////////////
  // PendingWork interface.

  void EnqueueMetadata(storage::Operation operation, storage::BlockBuffer* buffer) final;
  void EnqueueData(storage::Operation operation, storage::BlockBuffer* buffer) final;

  size_t AllocateBlock() final { return block_reservation_->Allocate(); }

  void DeallocateBlock(size_t block) final { return block_reservation_->Deallocate(block); }

  ////////////////
  // Other methods.
  size_t AllocateInode() { return inode_reservation_.Allocate(); }

  void PinVnode(fbl::RefPtr<VnodeMinfs> vnode);

  // Extends block reservation by |reserve_blocks| number of blocks. It may fail
  // if the underlying allocator runs out of space.
  zx_status_t ExtendBlockReservation(size_t reserve_blocks);

#ifdef __Fuchsia__
  // Returns a vector of all enqueued metadata write operations.
  std::vector<storage::UnbufferedOperation> RemoveMetadataOperations() {
    return metadata_operations_.TakeOperations();
  }

  // Returns a vector of all enqueued data write operations.
  std::vector<storage::UnbufferedOperation> RemoveDataOperations() {
    return data_operations_.TakeOperations();
  }

  size_t SwapBlock(size_t old_bno) { return block_reservation_->Swap(old_bno); }

  std::vector<fbl::RefPtr<VnodeMinfs>> RemovePinnedVnodes();

  // Returns the block reservations within |transaction| and consumes |transaction|.
  // Asserts that there are no inode reservations.
  static std::unique_ptr<AllocatorReservation> TakeBlockReservations(
      std::unique_ptr<Transaction> transaction) {
    // When consuming transaction, we ignore any pending data and matadata operations
    // as they will be enqueued again.
    ZX_ASSERT(transaction->inode_reservation_.GetReserved() == 0);
    return (std::move(transaction->block_reservation_));
  }

#else
  std::vector<storage::BufferedOperation> TakeOperations() { return builder_.TakeOperations(); }
#endif

 private:
#ifdef __Fuchsia__
  fbl::AutoLock<fbl::Mutex> lock_;
  storage::UnbufferedOperationsBuilder metadata_operations_;
  storage::UnbufferedOperationsBuilder data_operations_;
  std::vector<fbl::RefPtr<VnodeMinfs>> pinned_vnodes_;
#else
  fs::BufferedOperationsBuilder builder_;
#endif

  AllocatorReservation inode_reservation_;
  std::unique_ptr<AllocatorReservation> block_reservation_;
};

}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_WRITEBACK_H_
