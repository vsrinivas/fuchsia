// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file describes the in-memory structures which construct
// a MinFS filesystem.

#pragma once

#include <inttypes.h>

#ifdef __Fuchsia__
#include <fbl/auto_lock.h>
#include <fs/managed-vfs.h>
#include <fs/remote.h>
#include <fs/watcher.h>
#include <sync/completion.h>
#include <lib/zx/vmo.h>
#endif

#include <fbl/algorithm.h>
#include <fbl/function.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/macros.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>

#include <fs/block-txn.h>
#include <fs/mapped-vmo.h>
#include <fs/ticker.h>
#include <fs/trace.h>
#include <fs/vfs.h>
#include <fs/vnode.h>

#include <zircon/misc/fnv1hash.h>

#include <minfs/format.h>
#include <minfs/writeback.h>

#include "allocator.h"
#include "inode-manager.h"
#include "superblock.h"

#ifdef __Fuchsia__
#include "metrics.h"
#endif

#define EXTENT_COUNT 5

// A compile-time debug check, which, if enabled, causes
// inline functions to be expanded to error checking code.
// Since this may be expensive, it is typically turned
// off, except for debugging.
// #define MINFS_PARANOID_MODE

namespace minfs {

#ifdef __Fuchsia__
using RawBitmap = bitmap::RawBitmapGeneric<bitmap::VmoStorage>;
#else
using RawBitmap = bitmap::RawBitmapGeneric<bitmap::DefaultStorage>;
#endif

#ifdef __Fuchsia__
// Validate that |vmo| is large enough to access block |blk|,
// relative to the start of the vmo.
inline void validate_vmo_size(zx_handle_t vmo, blk_t blk) {
#ifdef MINFS_PARANOID_MODE
    uint64_t size;
    size_t min = (blk + 1) * kMinfsBlockSize;
    ZX_ASSERT(zx_vmo_get_size(vmo, &size) == ZX_OK);
    ZX_ASSERT_MSG(size >= min, "VMO size %" PRIu64 " too small for access at block %u\n",
                  size, blk);
#endif // MINFS_PARANOID_MODE
}
#endif // __Fuchsia__

// minfs_sync_vnode flags
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
    BlockOffsets(const Bcache* bc, const Superblock* sb);

    blk_t IbmStartBlock() const { return ibm_start_block_; }
    blk_t IbmBlockCount() const { return ibm_block_count_; }

    blk_t AbmStartBlock() const { return abm_start_block_; }
    blk_t AbmBlockCount() const { return abm_block_count_; }

    blk_t InoStartBlock() const { return ino_start_block_; }
    blk_t InoBlockCount() const { return ino_block_count_; }

    blk_t DatStartBlock() const { return dat_start_block_; }
    blk_t DatBlockCount() const { return dat_block_count_; }

private:
    blk_t ibm_start_block_;
    blk_t ibm_block_count_;

    blk_t abm_start_block_;
    blk_t abm_block_count_;

    blk_t ino_start_block_;
    blk_t ino_block_count_;

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

    static zx_status_t Create(fbl::unique_ptr<Bcache> bc, const minfs_info_t* info,
                              fbl::unique_ptr<Minfs>* out);

    // instantiate a vnode from an inode
    // the inode must exist in the file system
    zx_status_t VnodeGet(fbl::RefPtr<VnodeMinfs>* out, ino_t ino);

    // instantiate a vnode with a new inode
    zx_status_t VnodeNew(WritebackWork* wb, fbl::RefPtr<VnodeMinfs>* out, uint32_t type);

    // Insert, lookup, and remove vnode from hash map.
    void VnodeInsert(VnodeMinfs* vn) __TA_EXCLUDES(hash_lock_);
    fbl::RefPtr<VnodeMinfs> VnodeLookup(uint32_t ino) __TA_EXCLUDES(hash_lock_);
    void VnodeRelease(VnodeMinfs* vn) __TA_EXCLUDES(hash_lock_);

    // Allocate a new data block.
    zx_status_t BlockNew(WriteTxn* txn, blk_t* out_bno);

    // Free a data block.
    void BlockFree(WriteTxn* txn, blk_t bno);

    // Free ino in inode bitmap, release all blocks held by inode.
    zx_status_t InoFree(VnodeMinfs* vn, WritebackWork* wb);

    // Writes back an inode into the inode table on persistent storage.
    // Does not modify inode bitmap.
    void InodeUpdate(WriteTxn* txn, ino_t ino, const minfs_inode_t* inode) {
        inodes_->Update(txn, ino, inode);
    }

    // Reads an inode from the inode table into memory.
    void InodeLoad(ino_t ino, minfs_inode_t* out) const {
        inodes_->Load(ino, out);
    }

    void ValidateBno(blk_t bno) const {
        ZX_DEBUG_ASSERT(bno != 0);
        ZX_DEBUG_ASSERT(bno < Info().block_count);
    }

    zx_status_t CreateWork(fbl::unique_ptr<WritebackWork>* out);

    void EnqueueWork(fbl::unique_ptr<WritebackWork> work) {
#ifdef __Fuchsia__
        writeback_->Enqueue(fbl::move(work));
#else
        work->Complete();
#endif
    }

#ifdef __Fuchsia__
    void SetUnmountCallback(fbl::Closure closure) { on_unmount_ = fbl::move(closure); }
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
    // Print information about filesystem metrics.
    void DumpMetrics() const;

    // Return an immutable reference to a copy of the internal info.
    const minfs_info_t& Info() const {
        return sb_->Info();
    }

    // TODO(rvargas): Make private.
    fbl::unique_ptr<Bcache> bc_;

private:
    // Fsck can introspect Minfs
    friend class MinfsChecker;
    using HashTable = fbl::HashTable<ino_t, VnodeMinfs*>;

#ifdef __Fuchsia__
    Minfs(fbl::unique_ptr<Bcache> bc, fbl::unique_ptr<Superblock> sb,
          fbl::unique_ptr<Allocator> block_allocator,
          fbl::unique_ptr<InodeManager> inodes,
          fbl::unique_ptr<WritebackBuffer> writeback,
          uint64_t fs_id);
#else
    Minfs(fbl::unique_ptr<Bcache> bc, fbl::unique_ptr<Superblock> sb,
          fbl::unique_ptr<Allocator> block_allocator,
          fbl::unique_ptr<InodeManager> inodes, BlockOffsets offsets);
#endif

    // Find a free inode, allocate it in the inode bitmap, and write it back to disk
    zx_status_t InoNew(WriteTxn* txn, const minfs_inode_t* inode, ino_t* out_ino);

    // Enqueues an update to the super block.
    void WriteInfo(WriteTxn* txn);

    // Creates an unique identifier for this instance. This is to be called only during
    // "construction".
    static zx_status_t CreateFsId(uint64_t* out);

#ifndef __Fuchsia__
    zx_status_t ReadBlk(blk_t bno, blk_t start, blk_t soft_max, blk_t hard_max, void* data);
#endif

    // Global information about the filesystem.
    fbl::unique_ptr<Superblock> sb_;
    fbl::unique_ptr<Allocator> block_allocator_;
    fbl::unique_ptr<InodeManager> inodes_;

    // Vnodes exist in the hash table as long as one or more reference exists;
    // when the Vnode is deleted, it is immediately removed from the map.
#ifdef __Fuchsia__
    fbl::Mutex hash_lock_;
#endif
    HashTable vnode_hash_ __TA_GUARDED(hash_lock_){};

    bool collecting_metrics_ = false;
#ifdef __Fuchsia__
    fbl::Closure on_unmount_{};
    MinfsMetrics metrics_ = {};
    fbl::unique_ptr<WritebackBuffer> writeback_;
    uint64_t fs_id_{};
#else
    // Store start block + length for all extents. These may differ from info block for
    // sparse files.
    BlockOffsets offsets_;
#endif
};

struct DirArgs {
    fbl::StringPiece name;
    ino_t ino;
    uint32_t type;
    uint32_t reclen;
    WritebackWork* wb;
};

struct DirectoryOffset {
    size_t off;      // Offset in directory of current record
    size_t off_prev; // Offset in directory of previous record
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
    static zx_status_t Allocate(Minfs* fs, uint32_t type, fbl::RefPtr<VnodeMinfs>* out);

    // Allocates a Vnode, loading |ino| from storage.
    //
    // Doesn't update create / modify times of the node.
    static zx_status_t Recreate(Minfs* fs, ino_t ino, fbl::RefPtr<VnodeMinfs>* out);

    bool IsDirectory() const { return inode_.magic == kMinfsMagicDir; }
    bool IsUnlinked() const { return inode_.link_count == 0; }
    zx_status_t CanUnlink() const;

    const minfs_inode_t* GetInode() const { return &inode_; }

    ino_t GetKey() const { return ino_; }
    // Should only be called once for the VnodeMinfs lifecycle.
    void SetIno(ino_t ino);
    static size_t GetHash(ino_t key) { return fnv1a_tiny(key, kMinfsHashBits); }

    // fs::Vnode interface (invoked publicly).
    zx_status_t Open(uint32_t flags, fbl::RefPtr<Vnode>* out_redirect) final;
    zx_status_t Close() final;

    // fbl::Recyclable interface.
    void fbl_recycle() final;

    // TODO(rvargas): Make private.
    Minfs* const fs_;

private:
    // Fsck can introspect Minfs
    friend class MinfsChecker;
    friend zx_status_t Minfs::InoFree(VnodeMinfs* vn, WritebackWork* wb);

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
    zx_status_t Ioctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                      size_t out_len, size_t* out_actual) final;

    // Internal functions
    zx_status_t ReadInternal(void* data, size_t len, size_t off, size_t* actual);
    zx_status_t ReadExactInternal(void* data, size_t len, size_t off);
    zx_status_t WriteInternal(WritebackWork* wb, const void* data, size_t len,
                              size_t off, size_t* actual);
    zx_status_t WriteExactInternal(WritebackWork* wb, const void* data, size_t len,
                                   size_t off);
    zx_status_t TruncateInternal(WritebackWork* wb, size_t len);
    // Lookup which can traverse '..'
    zx_status_t LookupInternal(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name);

    // Verify that the 'newdir' inode is not a subdirectory of this Vnode.
    // Traces the path from newdir back to the root inode.
    zx_status_t CheckNotSubdirectory(fbl::RefPtr<VnodeMinfs> newdir);

    using DirentCallback = zx_status_t (*)(fbl::RefPtr<VnodeMinfs>,
                                           minfs_dirent_t*, DirArgs*,
                                           DirectoryOffset*);

    // Enumerates directories.
    zx_status_t ForEachDirent(DirArgs* args, const DirentCallback func);

    // Directory callback functions.
    //
    // The following functions are passable to |ForEachDirent|, which reads the parent directory,
    // one dirent at a time, and passes each entry to the callback function, along with the DirArgs
    // information passed to the initial call of |ForEachDirent|.
    static zx_status_t DirentCallbackFind(fbl::RefPtr<VnodeMinfs>, minfs_dirent_t*, DirArgs*,
                                          DirectoryOffset*);
    static zx_status_t DirentCallbackUnlink(fbl::RefPtr<VnodeMinfs>, minfs_dirent_t*, DirArgs*,
                                            DirectoryOffset*);
    static zx_status_t DirentCallbackForceUnlink(fbl::RefPtr<VnodeMinfs>, minfs_dirent_t*, DirArgs*,
                                                 DirectoryOffset*);
    static zx_status_t DirentCallbackAttemptRename(fbl::RefPtr<VnodeMinfs>, minfs_dirent_t*,
                                                   DirArgs*, DirectoryOffset*);
    static zx_status_t DirentCallbackUpdateInode(fbl::RefPtr<VnodeMinfs>, minfs_dirent_t*, DirArgs*,
                                                 DirectoryOffset*);
    static zx_status_t DirentCallbackAppend(fbl::RefPtr<VnodeMinfs>, minfs_dirent_t*, DirArgs*,
                                            DirectoryOffset*);

    zx_status_t UnlinkChild(WritebackWork* wb, fbl::RefPtr<VnodeMinfs> child,
                            minfs_dirent_t* de, DirectoryOffset* offs);
    // Remove the link to a vnode (referring to inodes exclusively).
    // Has no impact on direntries (or parent inode).
    void RemoveInodeLink(WritebackWork* wb);

    // Although file sizes don't need to be block-aligned, the underlying VMO is
    // always kept at a size which is a multiple of |kMinfsBlockSize|.
    //
    // When a Vnode is truncated to a size larger than |inode_.size|, it is
    // assumed that any space between |inode_.size| and the nearest block is
    // filled with zeroes in the internal VMO. This function validates that
    // assumption.
    inline void ValidateVmoTail() const {
#if defined(MINFS_PARANOID_MODE) && defined(__Fuchsia__)
        if (!vmo_.is_valid()) {
            return;
        }

        // Verify that everything not allocated to "inode_.size" in the
        // last block is filled with zeroes.
        char buf[kMinfsBlockSize];
        const size_t vmo_size = fbl::round_up(inode_.size, kMinfsBlockSize);
        ZX_ASSERT(vmo_.read(buf, inode_.size, vmo_size - inode_.size) == ZX_OK);
        for (size_t i = 0; i < vmo_size - inode_.size; i++) {
            ZX_ASSERT_MSG(buf[i] == 0, "vmo[%" PRIu64 "] != 0 (inode size = %u)\n",
                          inode_.size + i, inode_.size);
        }
#endif  // MINFS_PARANOID_MODE && __Fuchsia__
    }

    typedef enum {
        READ,
        WRITE,
        DELETE,
    } blk_op_t;

    typedef struct bop_params {
        bop_params(blk_t start, blk_t count, blk_t* bnos)
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
    } bop_params_t;

    class DirectArgs {
    public:
        DirectArgs(blk_op_t op, blk_t* array, blk_t count, blk_t* bnos)
            : op_(op), array_(array), count_(count), bnos_(bnos), dirty_(false) {}

        blk_op_t GetOp() const { return op_; }
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

        bool IsDirty() const { return dirty_; }
    protected:
        const blk_op_t op_; // determines what operation to perform on blocks
        blk_t* const array_; // array containing blocks to be operated on
        const blk_t count_; // number of direct blocks to operate on
        blk_t* const bnos_; // array of |count| bnos returned to the user
        bool dirty_; // true if blocks have successfully been op'd
    };

    class IndirectArgs : public DirectArgs {
    public:
        IndirectArgs(blk_op_t op, blk_t* array, blk_t count, blk_t* bnos, blk_t bindex,
                     blk_t ib_vmo_offset)
            : DirectArgs(op, array, count, bnos), bindex_(bindex), ib_vmo_offset_(ib_vmo_offset) {}

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
        DindirectArgs(blk_op_t op, blk_t* array, blk_t count, blk_t* bnos, blk_t bindex,
                      blk_t ib_vmo_offset, blk_t ibindex, blk_t dib_vmo_offset)
            : IndirectArgs(op, array, count, bnos, bindex, ib_vmo_offset),
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
    zx_status_t AllocateIndirect(WritebackWork* wb, blk_t index, IndirectArgs* args);

    // Perform operation |op| on blocks as specified by |params|
    // The BlockOp methods should not be called directly
    // All BlockOp methods assume that vmo_indirect_ has been grown to the required size
    zx_status_t BlockOp(WritebackWork* wb, blk_op_t op, bop_params_t* params);
    zx_status_t BlockOpDirect(WritebackWork* wb, DirectArgs* params);
    zx_status_t BlockOpIndirect(WritebackWork* wb, IndirectArgs* params);
    zx_status_t BlockOpDindirect(WritebackWork* wb, DindirectArgs* params);

    // Get the disk block 'bno' corresponding to the 'n' block
    // If 'txn' is non-null, new blocks are allocated for all un-allocated bnos.
    // This can be extended to retrieve multiple contiguous blocks in one call
    zx_status_t BlockGet(WritebackWork* wb, blk_t n, blk_t* bno);
    // Deletes all blocks (relative to a file) from "start" (inclusive) to the end
    // of the file. Does not update mtime/atime.
    // This can be extended to return indices of deleted bnos, or to delete a specific number of
    // bnos
    zx_status_t BlocksShrink(WritebackWork* wb, blk_t start);

    // Update the vnode's inode and write it to disk.
    void InodeSync(WritebackWork* wb, uint32_t flags);

    // Deletes this Vnode from disk, freeing the inode and blocks.
    //
    // Must only be called on Vnodes which
    // - Have no open fds
    // - Are fully unlinked (link count == 0)
    void Purge(WritebackWork* wb);

#ifdef __Fuchsia__
    void Sync(SyncCallback closure) final;
    zx_status_t AttachRemote(fs::MountChannel h) final;
    zx_status_t InitVmo();
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
    zx_status_t WatchDir(fs::Vfs* vfs, const vfs_watch_dir_t* cmd) final;

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

#ifdef __Fuchsia__
    // TODO(smklein): When we have can register MinFS as a pager service, and
    // it can properly handle pages faults on a vnode's contents, then we can
    // avoid reading the entire file up-front. Until then, read the contents of
    // a VMO into memory when it is read/written.
    zx::vmo vmo_{};

    // vmo_indirect_ contains all indirect and doubly indirect blocks in the following order:
    // First kMinfsIndirect blocks                                - initial set of indirect blocks
    // Next kMinfsDoublyIndirect blocks                           - doubly indirect blocks
    // Next kMinfsDoublyIndirect * kMinfsDirectPerIndirect blocks - indirect blocks pointed to
    //                                                              by doubly indirect blocks
    fbl::unique_ptr<fs::MappedVmo> vmo_indirect_{};

    vmoid_t vmoid_{};
    vmoid_t vmoid_indirect_{};

    fs::RemoteContainer remoter_{};
    fs::WatcherContainer watcher_{};
#endif

    ino_t ino_{};
    minfs_inode_t inode_{};

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
void minfs_sync_vnode(fbl::RefPtr<VnodeMinfs> vn, uint32_t flags);
void minfs_dump_info(const minfs_info_t* info);
void minfs_dump_inode(const minfs_inode_t* inode, ino_t ino);
void minfs_dir_init(void* bdata, ino_t ino_self, ino_t ino_parent);

// Given an input bcache, initialize the filesystem and return a reference to the
// root node.
zx_status_t minfs_mount(fbl::unique_ptr<minfs::Bcache> bc, fbl::RefPtr<VnodeMinfs>* root_out);

} // namespace minfs
