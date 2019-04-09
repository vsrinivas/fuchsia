// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file describes the in-memory structures which construct
// a MinFS filesystem.

#pragma once

#include <utility>

#include <inttypes.h>

#ifdef __Fuchsia__
#include <fbl/auto_lock.h>
#include <fs/managed-vfs.h>
#include <fs/remote.h>
#include <fs/watcher.h>
#include <fuchsia/io/c/fidl.h>
#include <fuchsia/minfs/c/fidl.h>
#include <lib/fzl/resizeable-vmo-mapper.h>
#include <lib/sync/completion.h>
#include <lib/zx/vmo.h>
#include <minfs/writeback-async.h>

#include "data-assigner.h"
#include "vnode-allocation.h"
#endif

#include <fbl/algorithm.h>
#include <fbl/function.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/macros.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <fs/block-txn.h>
#include <fs/locking.h>
#include <fs/ticker.h>
#include <fs/trace.h>
#include <fs/vfs.h>
#include <fs/vnode.h>
#include <lib/zircon-internal/fnv1hash.h>
#include <minfs/format.h>
#include <minfs/minfs.h>
#include <minfs/superblock.h>
#include <minfs/transaction-limits.h>
#include <minfs/writeback.h>

#include "allocator/allocator.h"
#include "allocator/inode-manager.h"
#include "vnode.h"

constexpr uint32_t kExtentCount = 6;

// A compile-time debug check, which, if enabled, causes
// inline functions to be expanded to error checking code.
// Since this may be expensive, it is typically turned
// off, except for debugging.
// #define MINFS_PARANOID_MODE

namespace minfs {
#ifdef __Fuchsia__
using BlockRegion = fuchsia_minfs_BlockRegion;

// Validate that |vmo| is large enough to access block |blk|,
// relative to the start of the vmo.
inline void ValidateVmoSize(zx_handle_t vmo, blk_t blk) {
#ifdef MINFS_PARANOID_MODE
    uint64_t size;
    size_t min = (blk + 1) * kMinfsBlockSize;
    ZX_ASSERT(zx_vmo_get_size(vmo, &size) == ZX_OK);
    ZX_ASSERT_MSG(size >= min, "VMO size %" PRIu64 " too small for access at block %u\n",
                  size, blk);
#endif // MINFS_PARANOID_MODE
}
#endif // __Fuchsia__

// SyncVnode flags
constexpr uint32_t kMxFsSyncDefault = 0; // default: no implicit time update
constexpr uint32_t kMxFsSyncMtime = (1 << 0);
constexpr uint32_t kMxFsSyncCtime = (1 << 1);

constexpr uint32_t kMinfsBlockCacheSize = 64;

// Used by fsck
class MinfsChecker;
class VnodeMinfs;

using SyncCallback = fs::Vnode::SyncCallback;

#ifndef __Fuchsia__
// Store start block + length for all extents. These may differ from info block for
// sparse files.
class BlockOffsets {
public:
    BlockOffsets(const Bcache& bc, const SuperblockManager& sb);

    blk_t IbmStartBlock() const { return ibm_start_block_; }
    blk_t IbmBlockCount() const { return ibm_block_count_; }

    blk_t AbmStartBlock() const { return abm_start_block_; }
    blk_t AbmBlockCount() const { return abm_block_count_; }

    blk_t InoStartBlock() const { return ino_start_block_; }
    blk_t InoBlockCount() const { return ino_block_count_; }

    blk_t JournalStartBlock() const { return journal_start_block_; }
    blk_t JournalBlockCount() const { return journal_block_count_; }

    blk_t DatStartBlock() const { return dat_start_block_; }
    blk_t DatBlockCount() const { return dat_block_count_; }

private:
    blk_t ibm_start_block_;
    blk_t ibm_block_count_;

    blk_t abm_start_block_;
    blk_t abm_block_count_;

    blk_t ino_start_block_;
    blk_t ino_block_count_;

    blk_t journal_start_block_;
    blk_t journal_block_count_;

    blk_t dat_start_block_;
    blk_t dat_block_count_;
};
#endif

class TransactionalFs {
public:
#ifdef __Fuchsia__
    virtual fbl::Mutex* GetLock() const = 0;

    void EnqueueCallback(SyncCallback callback) {
         fbl::unique_ptr<WritebackWork> work(new WritebackWork(GetMutableBcache()));
         work->SetSyncCallback(std::move(callback));
         EnqueueWork(std::move(work));
    }
#endif
    // Begin a transaction with |reserve_inodes| inodes and |reserve_blocks| blocks reserved.
    virtual zx_status_t BeginTransaction(size_t reserve_inodes, size_t reserve_blocks,
                                         fbl::unique_ptr<Transaction>* transaction_out) = 0;

    // Enqueues a WritebackWork for processing.
    virtual zx_status_t EnqueueWork(fbl::unique_ptr<WritebackWork> work) = 0;

    // Complete a transaction by persisting its contents to disk.
    virtual zx_status_t CommitTransaction(fbl::unique_ptr<Transaction> transaction) = 0;

    virtual Bcache* GetMutableBcache() = 0;
};

class Minfs :
#ifdef __Fuchsia__
    public fs::ManagedVfs,
#else
    public fs::Vfs,
#endif
    public fbl::RefCounted<Minfs>, public TransactionalFs {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Minfs);

    ~Minfs();

    static zx_status_t Create(fbl::unique_ptr<Bcache> bc, const Superblock* info,
                              fbl::unique_ptr<Minfs>* out, IntegrityCheck checks);

#ifdef __Fuchsia__
    // Initializes the Minfs writeback queue and resolves any pending disk state (e.g., resolving
    // unlinked nodes).
    zx_status_t InitializeWriteback();

    // Queries the underlying FVM, if it exists.
    zx_status_t FVMQuery(fuchsia_hardware_block_volume_VolumeInfo* info) const;
#endif

    // instantiate a vnode from an inode
    // the inode must exist in the file system
    zx_status_t VnodeGet(fbl::RefPtr<VnodeMinfs>* out, ino_t ino);

    // instantiate a vnode with a new inode
    zx_status_t VnodeNew(Transaction* transaction, fbl::RefPtr<VnodeMinfs>* out, uint32_t type);

    // Insert, lookup, and remove vnode from hash map.
    void VnodeInsert(VnodeMinfs* vn) FS_TA_EXCLUDES(hash_lock_);
    fbl::RefPtr<VnodeMinfs> VnodeLookup(uint32_t ino) FS_TA_EXCLUDES(hash_lock_);
    void VnodeRelease(VnodeMinfs* vn) FS_TA_EXCLUDES(hash_lock_);

    // Allocate a new data block.
    void BlockNew(Transaction* transaction, blk_t* out_bno);

    // Mark |in_bno| for de-allocation (if it is > 0), and return a new block |*out_bno|.
    // The swap will not be persisted until the transaction is commited.
    void BlockSwap(Transaction* transaction, blk_t in_bno, blk_t* out_bno);

    // Free a data block.
    void BlockFree(Transaction* transaction, blk_t bno);

    // Free ino in inode bitmap, release all blocks held by inode.
    zx_status_t InoFree(Transaction* transaction, VnodeMinfs* vn);

    // Mark |vn| to be unlinked.
    void AddUnlinked(Transaction* transaction, VnodeMinfs* vn);

    // Remove |vn| from the list of unlinked vnodes.
    void RemoveUnlinked(Transaction* transaction, VnodeMinfs* vn);

    // Free resources of all vnodes marked unlinked.
    zx_status_t PurgeUnlinked();

    // Writes back an inode into the inode table on persistent storage.
    // Does not modify inode bitmap.
    void InodeUpdate(WriteTxn* transaction, ino_t ino, const Inode* inode) {
        inodes_->Update(transaction, ino, inode);
    }

    // Reads an inode from the inode table into memory.
    void InodeLoad(ino_t ino, Inode* out) const {
        inodes_->Load(ino, out);
    }

    void ValidateBno(blk_t bno) const {
        ZX_DEBUG_ASSERT(bno != 0);
        ZX_DEBUG_ASSERT(bno < Info().block_count);
    }

    zx_status_t BeginTransaction(size_t reserve_inodes, size_t reserve_blocks,
                                 fbl::unique_ptr<Transaction>* transaction) __WARN_UNUSED_RESULT;

    zx_status_t EnqueueWork(fbl::unique_ptr<WritebackWork> work) final __WARN_UNUSED_RESULT;

    void EnqueueAllocation(fbl::unique_ptr<Transaction> transaction);

    // Complete a transaction by enqueueing its WritebackWork to the WritebackQueue.
    zx_status_t CommitTransaction(fbl::unique_ptr<Transaction> transaction) final
        __WARN_UNUSED_RESULT;

#ifdef __Fuchsia__
    // Returns the capacity of the writeback buffer, in blocks.
    size_t WritebackCapacity() const {
        ZX_DEBUG_ASSERT(writeback_ != nullptr);
        return writeback_->GetCapacity();
    }

    void SetUnmountCallback(fbl::Closure closure) { on_unmount_ = std::move(closure); }
    void Shutdown(fs::Vfs::ShutdownCallback cb) final;

    // Returns a unique identifier for this instance.
    uint64_t GetFsId() const { return fs_id_; }

    // Signals the completion object as soon as...
    // (1) A sync probe has entered and exited the writeback queue, and
    // (2) The block cache has sync'd with the underlying block device.
    void Sync(SyncCallback closure);
#endif

    // The following methods are used to read one block from the specified extent,
    // from relative block |bno|.
    // |data| is an out parameter that must be a block in size, provided by the caller
    // These functions are single-block and synchronous. On Fuchsia, using the batched read
    // functions is preferred.
    zx_status_t ReadDat(blk_t bno, void* data);

    void SetMetrics(bool enable) { collecting_metrics_ = enable; }
    fs::Ticker StartTicker() { return fs::Ticker(collecting_metrics_); }

    // Update aggregate information about VMO initialization.
    void UpdateInitMetrics(uint32_t dnum_count, uint32_t inum_count,
                           uint32_t dinum_count, uint64_t user_data_size,
                           const fs::Duration& duration);
    // Update aggregate information about looking up vnodes by name.
    void UpdateLookupMetrics(bool success, const fs::Duration& duration);
    // Update aggregate information about looking up vnodes by inode.
    void UpdateOpenMetrics(bool cache_hit, const fs::Duration& duration);
    // Update aggregate information about inode creation.
    void UpdateCreateMetrics(bool success, const fs::Duration& duration);
    // Update aggregate information about reading from Vnodes.
    void UpdateReadMetrics(uint64_t size, const fs::Duration& duration);
    // Update aggregate information about writing to Vnodes.
    void UpdateWriteMetrics(uint64_t size, const fs::Duration& duration);
    // Update aggregate information about truncating Vnodes.
    void UpdateTruncateMetrics(const fs::Duration& duration);
    // Update aggregate information about unlinking Vnodes.
    void UpdateUnlinkMetrics(bool success, const fs::Duration& duration);
    // Update aggregate information about renaming Vnodes.
    void UpdateRenameMetrics(bool success, const fs::Duration& duration);

#ifdef __Fuchsia__
    // Acquire a copy of the collected metrics.
    zx_status_t GetMetrics(fuchsia_minfs_Metrics* out) const {
        if (collecting_metrics_) {
            memcpy(out, &metrics_, sizeof(metrics_));
            return ZX_OK;
        }
        return ZX_ERR_UNAVAILABLE;
    }

    // Record the location, size, and number of all non-free block regions.
    fbl::Vector<BlockRegion> GetAllocatedRegions() const;
#endif

    // Return an immutable reference to a copy of the internal info.
    const Superblock& Info() const {
        return sb_->Info();
    }

    const TransactionLimits& Limits() const {
        return limits_;
    }

#ifdef __Fuchsia__
    fbl::Mutex* GetLock() const final { return &txn_lock_; }
#endif

    Bcache* GetMutableBcache() final { return bc_.get(); }

    // TODO(rvargas): Make private.
    fbl::unique_ptr<Bcache> bc_;

private:
    // Fsck can introspect Minfs
    friend class MinfsChecker;
    using HashTable = fbl::HashTable<ino_t, VnodeMinfs*>;

#ifdef __Fuchsia__
    Minfs(fbl::unique_ptr<Bcache> bc, fbl::unique_ptr<SuperblockManager> sb,
          fbl::unique_ptr<Allocator> block_allocator,
          fbl::unique_ptr<InodeManager> inodes,
          uint64_t fs_id);
#else
    Minfs(fbl::unique_ptr<Bcache> bc, fbl::unique_ptr<SuperblockManager> sb,
          fbl::unique_ptr<Allocator> block_allocator,
          fbl::unique_ptr<InodeManager> inodes, BlockOffsets offsets);
#endif

    // Internal version of VnodeLookup which may also return unlinked vnodes.
    fbl::RefPtr<VnodeMinfs> VnodeLookupInternal(uint32_t ino) FS_TA_EXCLUDES(hash_lock_);

    // Find a free inode, allocate it in the inode bitmap, and write it back to disk
    void InoNew(Transaction* transaction, const Inode* inode, ino_t* out_ino);

    // Enqueues an update to the super block.
    void WriteInfo(WriteTxn* transaction);

    // Find an unallocated and unreserved block in the block bitmap starting from block |start|
    zx_status_t FindBlock(size_t start, size_t* blkno_out);

    // Creates an unique identifier for this instance. This is to be called only during
    // "construction".
    static zx_status_t CreateFsId(uint64_t* out);

#ifndef __Fuchsia__
    zx_status_t ReadBlk(blk_t bno, blk_t start, blk_t soft_max, blk_t hard_max, void* data);
#endif

    // Global information about the filesystem.
    // While Allocator is thread-safe, it is recommended that a valid Transaction object be held
    // while any metadata fields are modified until the time they are enqueued for writeback. This
    // is to avoid modifications from other threads potentially jeopardizing the metadata integrity
    // before it is safely persisted to disk.
    fbl::unique_ptr<SuperblockManager> sb_;
    fbl::unique_ptr<Allocator> block_allocator_;
    fbl::unique_ptr<InodeManager> inodes_;

#ifdef __Fuchsia__
    mutable fbl::Mutex txn_lock_; // Lock required to start a new Transaction.
    fbl::Mutex hash_lock_; // Lock required to access the vnode_hash_.
#endif
    // Vnodes exist in the hash table as long as one or more reference exists;
    // when the Vnode is deleted, it is immediately removed from the map.
    HashTable vnode_hash_ FS_TA_GUARDED(hash_lock_){};

    bool collecting_metrics_ = false;
#ifdef __Fuchsia__
    fbl::Closure on_unmount_{};
    fuchsia_minfs_Metrics metrics_ = {};
    fbl::unique_ptr<WritebackQueue> writeback_;
    fbl::unique_ptr<DataBlockAssigner> assigner_;
    uint64_t fs_id_ = 0;
#else
    // Store start block + length for all extents. These may differ from info block for
    // sparse files.
    BlockOffsets offsets_;
#endif

    TransactionLimits limits_;
};

// Return the block offset in vmo_indirect_ of indirect blocks pointed to by the doubly indirect
// block at dindex
constexpr uint32_t GetVmoOffsetForIndirect(uint32_t dibindex) {
    return kMinfsIndirect + kMinfsDoublyIndirect + (dibindex * kMinfsDirectPerIndirect);
}

// Return the required vmo size (in bytes) to store indirect blocks pointed to by doubly indirect
// block dibindex
constexpr size_t GetVmoSizeForIndirect(uint32_t dibindex) {
    return GetVmoOffsetForIndirect(dibindex + 1) * kMinfsBlockSize;
}

// Return the block offset of doubly indirect blocks in vmo_indirect_
constexpr uint32_t GetVmoOffsetForDoublyIndirect(uint32_t dibindex) {
    ZX_DEBUG_ASSERT(dibindex < kMinfsDoublyIndirect);
    return kMinfsIndirect + dibindex;
}

// Return the required vmo size (in bytes) to store doubly indirect blocks in vmo_indirect_
constexpr size_t GetVmoSizeForDoublyIndirect() {
    return (kMinfsIndirect + kMinfsDoublyIndirect) * kMinfsBlockSize;
}

// write the inode data of this vnode to disk (default does not update time values)
void SyncVnode(fbl::RefPtr<VnodeMinfs> vn, uint32_t flags);
void DumpInfo(const Superblock* info);
void DumpInode(const Inode* inode, ino_t ino);
zx_time_t GetTimeUTC();
void InitializeDirectory(void* bdata, ino_t ino_self, ino_t ino_parent);

// Given an input bcache, initialize the filesystem and return a reference to the
// root node.
zx_status_t Mount(fbl::unique_ptr<minfs::Bcache> bc, const MountOptions& options,
                  fbl::RefPtr<VnodeMinfs>* root_out);
} // namespace minfs
