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

class Minfs :
#ifdef __Fuchsia__
    public fs::ManagedVfs,
#else
    public fs::Vfs,
#endif
    public fbl::RefCounted<Minfs> {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Minfs);

    ~Minfs();

    static zx_status_t Create(fbl::unique_ptr<Bcache> bc, const Superblock* info,
                              fbl::unique_ptr<Minfs>* out);

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

    // Begin a transaction with a WritebackWork, |reserve_inodes| inodes reserved,
    // and |reserve_blocks| blocks reserved.
    zx_status_t BeginTransaction(size_t reserve_inodes, size_t reserve_blocks,
                                 fbl::unique_ptr<Transaction>* transaction) __WARN_UNUSED_RESULT;

    // Enqueues a WritebackWork to the WritebackQueue.
    zx_status_t EnqueueWork(fbl::unique_ptr<WritebackWork> work) __WARN_UNUSED_RESULT;

    // Complete a transaction by enqueueing its WritebackWork to the WritebackQueue.
    zx_status_t CommitTransaction(fbl::unique_ptr<Transaction> transaction) __WARN_UNUSED_RESULT;

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
    fbl::Mutex* GetLock() const { return &txn_lock_; }
#endif

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
    uint64_t fs_id_ = 0;
#else
    // Store start block + length for all extents. These may differ from info block for
    // sparse files.
    BlockOffsets offsets_;
#endif

    TransactionLimits limits_;
};

struct DirectoryOffset {
    size_t off = 0;      // Offset in directory of current record
    size_t off_prev = 0; // Offset in directory of previous record
};

struct DirArgs {
    fbl::StringPiece name;
    ino_t ino;
    uint32_t type;
    uint32_t reclen;
    Transaction* transaction;
    DirectoryOffset offs;
};

class VnodeMinfs final : public fs::Vnode,
                         public fbl::SinglyLinkedListable<VnodeMinfs*>,
                         public fbl::Recyclable<VnodeMinfs> {
public:
    ~VnodeMinfs();

    // Allocates a new Vnode and initializes the in-memory inode structure given the type, where
    // type is one of:
    // - kMinfsTypeFile
    // - kMinfsTypeDir
    //
    // Sets create / modify times of the new node.
    // Does not allocate an inode number for the Vnode.
    static void Allocate(Minfs* fs, uint32_t type, fbl::RefPtr<VnodeMinfs>* out);

    // Allocates a Vnode, loading |ino| from storage.
    //
    // Doesn't update create / modify times of the node.
    static zx_status_t Recreate(Minfs* fs, ino_t ino, fbl::RefPtr<VnodeMinfs>* out);

    bool IsDirectory() const final { return inode_.magic == kMinfsMagicDir; }
    bool IsUnlinked() const { return inode_.link_count == 0; }
    zx_status_t CanUnlink() const;

    const Inode* GetInode() const { return &inode_; }

    ino_t GetKey() const { return ino_; }
    // Should only be called once for the VnodeMinfs lifecycle.
    void SetIno(ino_t ino);
    static size_t GetHash(ino_t key) { return fnv1a_tiny(key, kMinfsHashBits); }

    // fs::Vnode interface (invoked publicly).
#ifdef __Fuchsia__
    zx_status_t Serve(fs::Vfs* vfs, zx::channel channel, uint32_t flags) final;
#endif
    zx_status_t Open(uint32_t flags, fbl::RefPtr<Vnode>* out_redirect) final;
    zx_status_t Close() final;

    // fbl::Recyclable interface.
    void fbl_recycle() final;

#ifdef __Fuchsia__
    // Minfs FIDL interface.
    zx_status_t GetMetrics(fidl_txn_t* transaction);
    zx_status_t ToggleMetrics(bool enabled, fidl_txn_t* transaction);
    zx_status_t GetAllocatedRegions(fidl_txn_t* transaction) const;

    // Using the provided |transaction|, allocate all data blocks pending in |allocation_state_|.
    void AllocateData(Transaction* transaction);
#endif

    Minfs* Vfs() { return fs_; }

private:
    // Fsck can introspect Minfs
    friend class MinfsChecker;
    friend zx_status_t Minfs::InoFree(Transaction* transaction, VnodeMinfs* vn);
    friend void Minfs::AddUnlinked(Transaction* transaction, VnodeMinfs* vn);
    friend void Minfs::RemoveUnlinked(Transaction* transaction, VnodeMinfs* vn);

    VnodeMinfs(Minfs* fs);

    // fs::Vnode interface.
    zx_status_t ValidateFlags(uint32_t flags) final;
    zx_status_t Lookup(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name) final;
    zx_status_t Read(void* data, size_t len, size_t off, size_t* out_actual) final;
    zx_status_t Write(const void* data, size_t len, size_t offset,
                      size_t* out_actual) final;
    zx_status_t Append(const void* data, size_t len, size_t* out_end,
                       size_t* out_actual) final;
    zx_status_t Getattr(vnattr_t* a) final;
    zx_status_t Setattr(const vnattr_t* a) final;
    zx_status_t Readdir(fs::vdircookie_t* cookie, void* dirents, size_t len,
                        size_t* out_actual) final;
    zx_status_t Create(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name,
                       uint32_t mode) final;
    zx_status_t Unlink(fbl::StringPiece name, bool must_be_dir) final;
    zx_status_t Rename(fbl::RefPtr<fs::Vnode> newdir,
                       fbl::StringPiece oldname, fbl::StringPiece newname,
                       bool src_must_be_dir, bool dst_must_be_dir) final;
    zx_status_t Link(fbl::StringPiece name, fbl::RefPtr<fs::Vnode> target) final;
    zx_status_t Truncate(size_t len) final;
#ifdef __Fuchsia__
    zx_status_t QueryFilesystem(fuchsia_io_FilesystemInfo* out) final;
    zx_status_t GetDevicePath(size_t buffer_len, char* out_name, size_t* out_len) final;
#endif

    // Internal functions
    zx_status_t ReadInternal(Transaction* transaction, void* data, size_t len, size_t off,
                             size_t* actual);
    zx_status_t ReadExactInternal(Transaction* transaction, void* data, size_t len, size_t off);
    zx_status_t WriteInternal(Transaction* transaction, const void* data, size_t len,
                              size_t off, size_t* actual);
    zx_status_t WriteExactInternal(Transaction* transaction, const void* data, size_t len,
                                   size_t off);
    zx_status_t TruncateInternal(Transaction* transaction, size_t len);
    // Lookup which can traverse '..'
    zx_status_t LookupInternal(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name);

    // Verify that the 'newdir' inode is not a subdirectory of this Vnode.
    // Traces the path from newdir back to the root inode.
    zx_status_t CheckNotSubdirectory(fbl::RefPtr<VnodeMinfs> newdir);

    using DirentCallback = zx_status_t (*)(fbl::RefPtr<VnodeMinfs>, Dirent*, DirArgs*);

    // Enumerates directories.
    zx_status_t ForEachDirent(DirArgs* args, const DirentCallback func);

    // Directory callback functions.
    //
    // The following functions are passable to |ForEachDirent|, which reads the parent directory,
    // one dirent at a time, and passes each entry to the callback function, along with the DirArgs
    // information passed to the initial call of |ForEachDirent|.
    static zx_status_t DirentCallbackFind(fbl::RefPtr<VnodeMinfs>, Dirent*, DirArgs*);
    static zx_status_t DirentCallbackUnlink(fbl::RefPtr<VnodeMinfs>, Dirent*, DirArgs*);
    static zx_status_t DirentCallbackForceUnlink(fbl::RefPtr<VnodeMinfs>, Dirent*,
                                                 DirArgs*);
    static zx_status_t DirentCallbackAttemptRename(fbl::RefPtr<VnodeMinfs>, Dirent*,
                                                   DirArgs*);
    static zx_status_t DirentCallbackUpdateInode(fbl::RefPtr<VnodeMinfs>, Dirent*,
                                                 DirArgs*);
    static zx_status_t DirentCallbackFindSpace(fbl::RefPtr<VnodeMinfs>, Dirent*, DirArgs*);

    // Appends a new directory at the specified offset within |args|. This requires a prior call to
    // DirentCallbackFindSpace to find an offset where there is space for the direntry. It takes
    // the same |args| that were passed into DirentCallbackFindSpace.
    zx_status_t AppendDirent(DirArgs* args);

    zx_status_t UnlinkChild(Transaction* transaction, fbl::RefPtr<VnodeMinfs> child,
                            Dirent* de, DirectoryOffset* offs);
    // Remove the link to a vnode (referring to inodes exclusively).
    // Has no impact on direntries (or parent inode).
    void RemoveInodeLink(Transaction* transaction);

    // Although file sizes don't need to be block-aligned, the underlying VMO is
    // always kept at a size which is a multiple of |kMinfsBlockSize|.
    //
    // When a Vnode is truncated to a size larger than |inode_.size|, it is
    // assumed that any space between |inode_.size| and the nearest block is
    // filled with zeroes in the internal VMO. This function validates that
    // assumption.
    void ValidateVmoTail(blk_t inode_size) const;

    enum class BlockOp {
        kRead,
        kWrite,
        kDelete,
        kSwap,
    };

    struct BlockOpArgs {
        BlockOpArgs(blk_t start, blk_t count, blk_t* bnos)
            : start(start), count(count), bnos(bnos) {
                // Initialize output array to 0 in case the indirect block(s) containing these bnos
                // do not exist
                if (bnos) {
                    memset(bnos, 0, sizeof(blk_t) * count);
                }
            }

        blk_t start;
        blk_t count;
        blk_t* bnos;
    };

    class DirectArgs {
    public:
        DirectArgs(BlockOp op, blk_t* array, blk_t count, blk_t rel_bno, blk_t* bnos)
            : array_(array), bnos_(bnos), count_(count), rel_bno_(rel_bno), op_(op),
              dirty_(false) {}

        BlockOp GetOp() const { return op_; }
        blk_t GetBno(blk_t index) const { return array_[index]; }
        void SetBno(blk_t index, blk_t value) {
            ZX_DEBUG_ASSERT(index < GetCount());

            if (bnos_ != nullptr) {
                bnos_[index] = value ? value : array_[index];
            }

            if (array_[index] != value) {
                array_[index] = value;
                dirty_ = true;
            }
        }

        blk_t GetCount() const { return count_; }
        blk_t GetRelativeBlock() const { return rel_bno_; }

        bool IsDirty() const { return dirty_; }
    protected:
        blk_t* const array_; // array containing blocks to be operated on
        blk_t* const bnos_; // array of |count| bnos returned to the user
        const blk_t count_; // number of direct blocks to operate on
        const blk_t rel_bno_; // The relative bno of the first direct block we are op'ing.
        const BlockOp op_; // determines what operation to perform on blocks
        bool dirty_; // true if blocks have successfully been op'd
    };

    class IndirectArgs : public DirectArgs {
    public:
        IndirectArgs(BlockOp op, blk_t* array, blk_t count, blk_t rel_bno,
                     blk_t* bnos, blk_t bindex, blk_t ib_vmo_offset)
            : DirectArgs(op, array, count, rel_bno, bnos), bindex_(bindex),
              ib_vmo_offset_(ib_vmo_offset) {}

        void SetDirty() { dirty_ = true; }

        void SetBno(blk_t index, blk_t value) {
            ZX_DEBUG_ASSERT(index < GetCount());
            array_[index] = value;
            SetDirty();
        }

        // Number of indirect blocks we need to iterate through to touch all |count| direct blocks.
        blk_t GetCount() const {
            return (bindex_ + count_ + kMinfsDirectPerIndirect - 1) / kMinfsDirectPerIndirect;
        }

        blk_t GetOffset() const { return ib_vmo_offset_; }

        // Generate parameters for direct blocks in indirect block |ibindex|, which are contained
        // in |barray|
        DirectArgs GetDirect(blk_t* barray, unsigned ibindex) const;

    protected:
        const blk_t bindex_; // relative index of the first direct block within the first indirect
                             // block
        const blk_t ib_vmo_offset_; // index of the first indirect block
    };

    class DindirectArgs : public IndirectArgs {
    public:
        DindirectArgs(BlockOp op, blk_t* array, blk_t count, blk_t rel_bno, blk_t* bnos,
                      blk_t bindex, blk_t ib_vmo_offset, blk_t ibindex, blk_t dib_vmo_offset)
            : IndirectArgs(op, array, count, rel_bno, bnos, bindex, ib_vmo_offset),
              ibindex_(ibindex), dib_vmo_offset_(dib_vmo_offset) {}

        // Number of doubly indirect blocks we need to iterate through to touch all |count| direct
        // blocks.
        blk_t GetCount() const {
            return (ibindex_ + count_ + kMinfsDirectPerDindirect - 1) / kMinfsDirectPerDindirect;
        }

        blk_t GetOffset() const { return dib_vmo_offset_; }

        // Generate parameters for indirect blocks in doubly indirect block |dibindex|, which are
        // contained in |iarray|
        IndirectArgs GetIndirect(blk_t* iarray, unsigned dibindex) const;

    protected:
        const blk_t ibindex_; // relative index of the first indirect block within the first
                              // doubly indirect block
        const blk_t dib_vmo_offset_; // index of the first doubly indirect block
    };

    // Allocate an indirect or doubly indirect block at |offset| within the indirect vmo and clear
    // the in-memory block array
    // Assumes that vmo_indirect_ has already been initialized
    void AllocateIndirect(Transaction* transaction, blk_t index, IndirectArgs* args);

    // Perform operation |op| on blocks as specified by |params|
    // The BlockOp methods should not be called directly
    // All BlockOp methods assume that vmo_indirect_ has been grown to the required size
    zx_status_t ApplyOperation(Transaction* transaction, BlockOp op, BlockOpArgs* params);
    zx_status_t BlockOpDirect(Transaction* transaction, DirectArgs* params);
    zx_status_t BlockOpIndirect(Transaction* transaction, IndirectArgs* params);
    zx_status_t BlockOpDindirect(Transaction* transaction, DindirectArgs* params);

    // Get the disk block 'bno' corresponding to the 'n' block
    // If 'transaction' is non-null, new blocks are allocated for all un-allocated bnos.
    // This can be extended to retrieve multiple contiguous blocks in one call
    zx_status_t BlockGet(Transaction* transaction, blk_t n, blk_t* bno);
    // Deletes all blocks (relative to a file) from "start" (inclusive) to the end
    // of the file. Does not update mtime/atime.
    // This can be extended to return indices of deleted bnos, or to delete a specific number of
    // bnos
    zx_status_t BlocksShrink(Transaction* transaction, blk_t start);

    // Update the vnode's inode and write it to disk.
    void InodeSync(WritebackWork* wb, uint32_t flags);

    // Deletes this Vnode from disk, freeing the inode and blocks.
    //
    // Must only be called on Vnodes which
    // - Have no open fds
    // - Are fully unlinked (link count == 0)
    void Purge(Transaction* transaction);

    blk_t GetBlockCount() const;

    // Returns current size of vnode.
    blk_t GetSize() const;

    // Sets current size of vnode.
    void SetSize(blk_t new_size);

#ifdef __Fuchsia__
    zx_status_t GetNodeInfo(uint32_t flags, fuchsia_io_NodeInfo* info) final;

    // For all direct blocks in the range |start| to |start + count|, reserve specific blocks in
    // the allocator to be swapped in at the time the old blocks are swapped out. Indirect blocks
    // are expected to have been allocated previously.
    zx_status_t BlocksSwap(Transaction* state, blk_t start, blk_t count, blk_t* bno);

    void Sync(SyncCallback closure) final;
    zx_status_t AttachRemote(fs::MountChannel h) final;
    zx_status_t InitVmo(Transaction* transaction);
    zx_status_t InitIndirectVmo();

    // Loads indirect blocks up to and including the doubly indirect block at |index|.
    zx_status_t LoadIndirectWithinDoublyIndirect(uint32_t index);

    // Initializes the indirect VMO, grows it to |size| bytes, and reads |count| indirect
    // blocks from |iarray| into the indirect VMO, starting at block offset |offset|.
    zx_status_t LoadIndirectBlocks(blk_t* iarray, uint32_t count, uint32_t offset,
                                   uint64_t size);

    // Reads the block at |offset| in memory.
    // Assumes that vmo_indirect_ has already been initialized
    void ReadIndirectVmoBlock(uint32_t offset, uint32_t** entry);

    // Clears the block at |offset| in memory.
    // Assumes that vmo_indirect_ has already been initialized
    void ClearIndirectVmoBlock(uint32_t offset);

    // Use the watcher container to implement a directory watcher
    void Notify(fbl::StringPiece name, unsigned event) final;
    zx_status_t WatchDir(fs::Vfs* vfs, uint32_t mask, uint32_t options, zx::channel watcher) final;

    // The vnode is acting as a mount point for a remote filesystem or device.
    bool IsRemote() const final;
    zx::channel DetachRemote() final;
    zx_handle_t GetRemote() const final;
    void SetRemote(zx::channel remote) final;
#else  // !__Fuchsia__
    // Reads the block at |bno| on disk.
    void ReadIndirectBlock(blk_t bno, uint32_t* entry);

    // Clears the block at |bno| on disk.
    void ClearIndirectBlock(blk_t bno);
#endif

    Minfs* const fs_;
#ifdef __Fuchsia__
    // TODO(smklein): When we have can register MinFS as a pager service, and
    // it can properly handle pages faults on a vnode's contents, then we can
    // avoid reading the entire file up-front. Until then, read the contents of
    // a VMO into memory when it is read/written.
    zx::vmo vmo_{};
    uint64_t vmo_size_ = 0;

    // vmo_indirect_ contains all indirect and doubly indirect blocks in the following order:
    // First kMinfsIndirect blocks                                - initial set of indirect blocks
    // Next kMinfsDoublyIndirect blocks                           - doubly indirect blocks
    // Next kMinfsDoublyIndirect * kMinfsDirectPerIndirect blocks - indirect blocks pointed to
    //                                                              by doubly indirect blocks
    fbl::unique_ptr<fzl::ResizeableVmoMapper> vmo_indirect_;

    fuchsia_hardware_block_VmoID vmoid_{};
    fuchsia_hardware_block_VmoID vmoid_indirect_{};

    fs::RemoteContainer remoter_{};
    fs::WatcherContainer watcher_{};

    PendingAllocationData allocation_state_;
#endif

    ino_t ino_{};
    Inode inode_{};

    // This field tracks the current number of file descriptors with
    // an open reference to this Vnode. Notably, this is distinct from the
    // VnodeMinfs's own refcount, since there may still be filesystem
    // work to do after the last file descriptor has been closed.
    uint32_t fd_count_{};
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
void InitializeDirectory(void* bdata, ino_t ino_self, ino_t ino_parent);

// Given an input bcache, initialize the filesystem and return a reference to the
// root node.
zx_status_t Mount(fbl::unique_ptr<minfs::Bcache> bc, const MountOptions& options,
                  fbl::RefPtr<VnodeMinfs>* root_out);
} // namespace minfs
