// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MINFS_FILE_H_
#define SRC_STORAGE_MINFS_FILE_H_

#ifdef __Fuchsia__
#include "src/storage/minfs/vnode_allocation.h"
#endif

#include <lib/zircon-internal/fnv1hash.h>

#include <fbl/algorithm.h>
#include <fbl/ref_ptr.h>
#include <fs/trace.h>
#include <fs/vnode.h>

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

  // fs::Vnode interface.
  fs::VnodeProtocolSet GetProtocols() const final;
  zx_status_t Read(void* data, size_t len, size_t off, size_t* out_actual) final;
  zx_status_t Write(const void* data, size_t len, size_t offset, size_t* out_actual) final;
  zx_status_t Append(const void* data, size_t len, size_t* out_end, size_t* out_actual) final;
  zx_status_t Truncate(size_t len) final;

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
};

}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_FILE_H_
