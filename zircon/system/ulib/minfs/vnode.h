// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <utility>

#include <inttypes.h>

#ifdef __Fuchsia__
#include <fbl/auto_lock.h>
#include <fs/remote.h>
#include <fs/watcher.h>
#include <fuchsia/io/c/fidl.h>
#include <fuchsia/minfs/c/fidl.h>
#include <lib/fzl/resizeable-vmo-mapper.h>
#include <lib/zx/vmo.h>
#include <minfs/writeback-async.h>

#include "data-assigner.h"
#include "vnode-allocation.h"
#endif

#include <fbl/algorithm.h>
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
#include <minfs/transaction-limits.h>
#include <minfs/writeback.h>

namespace minfs {

// Used by fsck
class MinfsChecker;
class Minfs;

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

// This class exists as an interface into the VnodeMinfs for processing pending data block
// allocations. Defining this separately allows VnodeMinfs to be substituted for a simpler version
// in unit testing, and also prevents clients from abusing other parts of the VnodeMinfs public API.
class DataAssignableVnode : public fs::Vnode, public fbl::Recyclable<DataAssignableVnode> {
public:
    DataAssignableVnode() = default;
    virtual ~DataAssignableVnode() = default;

    virtual void fbl_recycle() = 0;

#ifdef __Fuchsia__
    // Allocate all data blocks pending in |allocation_state_|.
    virtual void AllocateData() = 0;

protected:
    // Describes pending allocation data for the vnode. This should only be accessed while a valid
    // Transaction object is held, as it may be modified asynchronously by the DataBlockAssigner
    // thread.
    PendingAllocationData allocation_state_;
#endif
};

class VnodeMinfs final : public DataAssignableVnode,
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
    ino_t GetIno() const { return ino_; }

    ino_t GetKey() const { return ino_; }
    // Should only be called once for the VnodeMinfs lifecycle.
    void SetIno(ino_t ino);

    void SetNextInode(ino_t ino) {
        inode_.next_inode = ino;
    }
    void SetLastInode(ino_t ino) {
        inode_.last_inode = ino;
    }

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

    // Allocate all data blocks pending in |allocation_state_|.
    void AllocateData() final;

#endif
    Minfs* Vfs() { return fs_; }

    // TODO: The following methods should be made private:
#ifdef __Fuchsia__
    void CancelDataWriteback() {
        DataAssignableVnode::allocation_state_.Reset(inode_.size);
    }
    zx_status_t InitIndirectVmo();
    // Reads the block at |offset| in memory.
    // Assumes that vmo_indirect_ has already been initialized
    void ReadIndirectVmoBlock(uint32_t offset, uint32_t** entry);
    // Loads indirect blocks up to and including the doubly indirect block at |index|.
    zx_status_t LoadIndirectWithinDoublyIndirect(uint32_t index);
#else
    // Reads the block at |bno| on disk.
    void ReadIndirectBlock(blk_t bno, uint32_t* entry);
#endif

    // Update the vnode's inode and write it to disk.
    void InodeSync(WritebackWork* wb, uint32_t flags);

private:
    // Fsck can introspect Minfs
    friend class MinfsChecker;

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

    // Transfers |requested| blocks from the provided |transaction|'s block promise and gives them
    // to the allocation_state_'s block promise. Additionally adds the vnode to |transaction| to be
    // processed later. |transaction| must be non-null and possess an initialized block promise
    // with at least as many reserved blocks as |block_count|.
    void ScheduleAllocation(Transaction* transaction, blk_t block_count);
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

    // Deletes this Vnode from disk, freeing the inode and blocks.
    //
    // Must only be called on Vnodes which
    // - Have no open fds
    // - Are fully unlinked (link count == 0)
    void Purge(Transaction* transaction);

    // Returns the current block count of the vnode. Must be accessed with |transaction| to ensure
    // that the count isn't modified asynchronously.
    blk_t GetBlockCount(const Transaction& transaction) const;

    // Returns current size of vnode.
    blk_t GetSize() const;

    // Sets current size of vnode.
    void SetSize(blk_t new_size);

#ifdef __Fuchsia__
    zx_status_t GetNodeInfo(uint32_t flags, fuchsia_io_NodeInfo* info) final;

    // For data vnodes, if the pending size differs from the actual inode size, resolve them by
    // setting the inode size to the pending size.
    void ResolveSize() {
        if (!IsDirectory() && inode_.size != allocation_state_.GetNodeSize()) {
            inode_.size = allocation_state_.GetNodeSize();
        }
    }

    // For all direct blocks in the range |start| to |start + count|, reserve specific blocks in
    // the allocator to be swapped in at the time the old blocks are swapped out. Indirect blocks
    // are expected to have been allocated previously.
    zx_status_t BlocksSwap(Transaction* state, blk_t start, blk_t count, blk_t* bno);

    void Sync(SyncCallback closure) final;
    zx_status_t AttachRemote(fs::MountChannel h) final;
    zx_status_t InitVmo(Transaction* transaction);

    // Initializes the indirect VMO, grows it to |size| bytes, and reads |count| indirect
    // blocks from |iarray| into the indirect VMO, starting at block offset |offset|.
    zx_status_t LoadIndirectBlocks(blk_t* iarray, uint32_t count, uint32_t offset,
                                   uint64_t size);

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
    // DataBlockAssigner may modify this field asynchronously, so a valid Transaction object must
    // be held before accessing it.
    fbl::unique_ptr<fzl::ResizeableVmoMapper> vmo_indirect_;

    fuchsia_hardware_block_VmoID vmoid_{};
    fuchsia_hardware_block_VmoID vmoid_indirect_{};

    fs::RemoteContainer remoter_{};
    fs::WatcherContainer watcher_{};
#endif

    ino_t ino_{};

    // DataBlockAssigner may modify this field asynchronously, so a valid Transaction object must
    // be held before accessing it.
    Inode inode_{};

    // This field tracks the current number of file descriptors with
    // an open reference to this Vnode. Notably, this is distinct from the
    // VnodeMinfs's own refcount, since there may still be filesystem
    // work to do after the last file descriptor has been closed.
    uint32_t fd_count_{};
};

} // namespace minfs
