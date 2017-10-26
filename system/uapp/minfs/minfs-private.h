// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifdef __Fuchsia__
#include <fs/remote.h>
#include <fs/watcher.h>
#include <zx/vmo.h>
#endif

#include <fbl/algorithm.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/macros.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>

#include <fs/block-txn.h>
#include <fs/mapped-vmo.h>
#include <fs/trace.h>
#include <fs/vfs.h>
#include <fs/vnode.h>

#include <zircon/misc/fnv1hash.h>

#include <minfs/minfs.h>

#define EXTENT_COUNT 5

#define panic(fmt...)         \
    do {                      \
        fprintf(stderr, fmt); \
        __builtin_trap();     \
    } while (0)

// A compile-time debug check, which, if enabled, causes
// inline functions to be expanded to error checking code.
// Since this may be expensive, it is typically turned
// off, except for debugging.
// #define MINFS_PARANOID_MODE

namespace minfs {

#ifdef __Fuchsia__
// Validate that |vmo| is large enough to access block |blk|,
// relative to the start of the vmo.
inline void validate_vmo_size(zx_handle_t vmo, blk_t blk) {
#ifdef MINFS_PARANOID_MODE
    uint64_t size;
    size_t min = (blk + 1) * kMinfsBlockSize;
    ZX_ASSERT(zx_vmo_get_size(vmo, &size) == ZX_OK);
    ZX_ASSERT_MSG(size >= min, "VMO size %lu too small for access at block %lu\n",
                  size, blk);
#endif // MINFS_PARANOID_MODE
}
#endif // __Fuchsia__

extern fs::Vfs vfs;

using WriteTxn = fs::WriteTxn<kMinfsBlockSize, Bcache>;
using ReadTxn = fs::ReadTxn<kMinfsBlockSize, Bcache>;

// minfs_sync_vnode flags
constexpr uint32_t kMxFsSyncDefault = 0; // default: no implicit time update
constexpr uint32_t kMxFsSyncMtime = (1 << 0);
constexpr uint32_t kMxFsSyncCtime = (1 << 1);

constexpr uint32_t kMinfsBlockCacheSize = 64;

// Used by fsck
class MinfsChecker;

class VnodeMinfs;

class Minfs {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Minfs);

    ~Minfs();
    static zx_status_t Create(Minfs** out, fbl::unique_ptr<Bcache> bc, const minfs_info_t* info);

    zx_status_t Unmount();

    // instantiate a vnode from an inode
    // the inode must exist in the file system
    zx_status_t VnodeGet(fbl::RefPtr<VnodeMinfs>* out, ino_t ino);

    // instantiate a vnode with a new inode
    zx_status_t VnodeNew(WriteTxn* txn, fbl::RefPtr<VnodeMinfs>* out, uint32_t type);

    // remove a vnode from the hash map
    void VnodeRelease(VnodeMinfs* vn);

    // Allocate a new data block.
    zx_status_t BlockNew(WriteTxn* txn, blk_t hint, blk_t* out_bno);

    // free block in block bitmap
    zx_status_t BlockFree(WriteTxn* txn, blk_t bno);

    // free ino in inode bitmap, release all blocks held by inode
    zx_status_t InoFree(VnodeMinfs* vn);

    // Writes back an inode into the inode table on persistent storage.
    // Does not modify inode bitmap.
    zx_status_t InodeSync(WriteTxn* txn, ino_t ino, const minfs_inode_t* inode);

    void ValidateBno(blk_t bno) const {
        ZX_DEBUG_ASSERT(bno != 0);
        ZX_DEBUG_ASSERT(bno < info_.block_count);
    }

    fbl::unique_ptr<Bcache> bc_{};
    minfs_info_t info_{};

    // The following methods are used to read one block from the specified extent,
    // from relative block |bno|.
    // |data| is an out parameter that must be a block in size, provided by the caller
    // These functions are single-block and synchronous. On Fuchsia, using the batched read
    // functions is preferred.
    zx_status_t ReadIbm(blk_t bno, void* data);
    zx_status_t ReadAbm(blk_t bno, void* data);
    zx_status_t ReadIno(blk_t bno, void* data);
    zx_status_t ReadDat(blk_t bno, void* data);

private:
    // Fsck can introspect Minfs
    friend class MinfsChecker;
    Minfs(fbl::unique_ptr<Bcache> bc_, const minfs_info_t* info_);

    // Find a free inode, allocate it in the inode bitmap, and write it back to disk
    zx_status_t InoNew(WriteTxn* txn, const minfs_inode_t* inode, ino_t* ino_out);

    // Enqueues an update for allocated inode/block counts
    zx_status_t CountUpdate(WriteTxn* txn);

    // If possible, attempt to resize the MinFS partition.
    zx_status_t AddInodes();
    zx_status_t AddBlocks();

    uint32_t abmblks_{};
    uint32_t ibmblks_{};
    uint32_t inoblks_{};
    RawBitmap inode_map_{};
    RawBitmap block_map_{};
#ifdef __Fuchsia__
    fbl::unique_ptr<MappedVmo> inode_table_{};
    fbl::unique_ptr<MappedVmo> info_vmo_{};
    vmoid_t inode_map_vmoid_{};
    vmoid_t block_map_vmoid_{};
    vmoid_t inode_table_vmoid_{};
    vmoid_t info_vmoid_{};
#else
    zx_status_t ReadBlk(blk_t bno, blk_t start, blk_t soft_max, blk_t hard_max, void* data);

    // Store start block + length for all extents. These may differ from info block for
    // sparse files.
    blk_t ibm_start_block;
    blk_t ibm_block_count;

    blk_t abm_start_block;
    blk_t abm_block_count;

    blk_t ino_start_block;
    blk_t ino_block_count;

    blk_t dat_start_block;
    blk_t dat_block_count;
#endif

    // Vnodes exist in the hash table as long as one or more reference exists;
    // when the Vnode is deleted, it is immediately removed from the map.
    using HashTable = fbl::HashTable<ino_t, VnodeMinfs*>;
    HashTable vnode_hash_{};
};

struct DirArgs {
    fbl::StringPiece name;
    ino_t ino;
    uint32_t type;
    uint32_t reclen;
    WriteTxn* txn;
};

struct DirectoryOffset {
    size_t off;      // Offset in directory of current record
    size_t off_prev; // Offset in directory of previous record
};

#define INO_HASH(ino) fnv1a_tiny(ino, kMinfsHashBits)

// clang-format off
constexpr uint32_t kMinfsFlagDeletedDirectory = 0x00000001;
// clang-format on

class VnodeMinfs final : public fs::Vnode, public fbl::SinglyLinkedListable<VnodeMinfs*> {
public:
    // Allocates a Vnode and initializes the inode given the type.
    static zx_status_t Allocate(Minfs* fs, uint32_t type, fbl::RefPtr<VnodeMinfs>* out);
    // Allocates a Vnode, but leaves the inode untouched, so it may be overwritten.
    static zx_status_t AllocateHollow(Minfs* fs, fbl::RefPtr<VnodeMinfs>* out);

    bool IsDirectory() const { return inode_.magic == kMinfsMagicDir; }
    bool IsDeletedDirectory() const { return flags_ & kMinfsFlagDeletedDirectory; }
    zx_status_t CanUnlink() const;

    ino_t GetKey() const { return ino_; }
    static size_t GetHash(ino_t key) { return INO_HASH(key); }

    zx_status_t UnlinkChild(WriteTxn* txn, fbl::RefPtr<VnodeMinfs> child,
                            minfs_dirent_t* de, DirectoryOffset* offs);
    // Remove the link to a vnode (referring to inodes exclusively).
    // Has no impact on direntries (or parent inode).
    void RemoveInodeLink(WriteTxn* txn);
    zx_status_t ReadInternal(void* data, size_t len, size_t off, size_t* actual);
    zx_status_t ReadExactInternal(void* data, size_t len, size_t off);
    zx_status_t WriteInternal(WriteTxn* txn, const void* data, size_t len,
                              size_t off, size_t* actual);
    zx_status_t WriteExactInternal(WriteTxn* txn, const void* data, size_t len,
                                   size_t off);
    zx_status_t TruncateInternal(WriteTxn* txn, size_t len);
    zx_status_t Ioctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                      size_t out_len, size_t* out_actual) final;
    zx_status_t Lookup(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name) final;
    // Lookup which can traverse '..'
    zx_status_t LookupInternal(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name);

    Minfs* fs_{};
    ino_t ino_{};
    minfs_inode_t inode_{};

    ~VnodeMinfs();

private:
    // Fsck can introspect Minfs
    friend class MinfsChecker;
    friend zx_status_t Minfs::InoFree(VnodeMinfs* vn);
    VnodeMinfs(Minfs* fs);

    // Implementing methods from the fs::Vnode, so MinFS vnodes may be utilized
    // by the VFS library.
    zx_status_t ValidateFlags(uint32_t flags) final;
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
    zx_status_t Sync() final;

#ifdef __Fuchsia__
    zx_status_t AttachRemote(fs::MountChannel h) final;
    zx_status_t InitVmo();
    zx_status_t InitIndirectVmo();
    // Loads indirect blocks up to and including the doubly indirect block at |index|
    zx_status_t LoadIndirectWithinDoublyIndirect(uint32_t index);
    // Initializes the indirect VMO, grows it to |size| bytes, and reads |count| indirect
    // blocks from |iarray| into the indirect VMO, starting at block offset |offset|.
    zx_status_t LoadIndirectBlocks(blk_t* iarray, uint32_t count, uint32_t offset,
                                   uint64_t size);
#endif

    // Get the disk block 'bno' corresponding to the 'nth' block relative to the start of the
    // current direct/indirect/doubly indirect block section.
    // Allocate the block if requested with a non-null "txn".
    zx_status_t GetBno(WriteTxn* txn, blk_t n, blk_t* bno);
    // Acquire (or allocate) a direct block |*bno|. If allocation occurs,
    // |*dirty| is set to true, and the inode block is written to disk.
    //
    // Example call for accessing the 0th direct block in the inode:
    // GetBnoDirect(txn, &inode_.dnum[0], &dirty);
    zx_status_t GetBnoDirect(WriteTxn* txn, blk_t* bno, bool* dirty);
    // Acquire (or allocate) a direct block |*bno| contained at index |bindex| within an indirect
    // block |*ibno|, which is allocated if necessary. If allocation of the indirect block occurs,
    // |*dirty| is set to true, and the indirect and inode blocks are written to disk.
    //
    // On Fuchsia, |ib_vmo_offset| contains the block offset of |*ibno| within the cached
    // indirect VMO. On other platforms, this argument may be ignored.
    //
    // Example call for accessing the 3rd direct block within the 2nd indirect block:
    // GetBnoIndirect(txn, 3, 2, &inode_.inum[2], &bno, &dirty);
    zx_status_t GetBnoIndirect(WriteTxn* txn, uint32_t bindex, uint32_t ib_vmo_offset,
                               blk_t* ibno, blk_t* bno, bool* dirty);
    // Acquire (or allocate) a direct block |*bno| contained at index |bindex| within a doubly
    // indirect block |*dibno|, at index |ibindex| within that indirect block. If allocation occurs,
    // |*dirty| is set to true, and the doubly indirect, indirect, and inode blocks are written to
    // disk. |dib_vmo_offset| and |ib_vmo_offset| are the offset of the doubly indirect block and
    // the doubly indirect block's indirect block set within the indirect VMO, respectively.
    //
    // Example call for accessing the 3rd direct block in the 2nd indirect block
    // in the 0th doubly indirect block:
    // GetBnoDoublyIndirect(txn, 2, 3, GetVmoOffsetForDoublyIndirect(0), GetVmoOffsetForIndirect(0),
    //                      &inode_.dinum[0], &bno, &dirty);
    zx_status_t GetBnoDoublyIndirect(WriteTxn* txn, uint32_t ibindex, uint32_t bindex,
                                     uint32_t dib_vmo_offset, uint32_t ib_vmo_offset,
                                     blk_t* dibno, blk_t* bno, bool* dirty);

    // Deletes all blocks (relative to a file) from "start" (inclusive) to the end
    // of the file. Does not update mtime/atime.
    zx_status_t BlocksShrink(WriteTxn* txn, blk_t start);
    // Shrink |count| direct blocks from the |barray| array of direct blocks. Sets |*dirty| to
    // true if anything is deleted.
    zx_status_t BlocksShrinkDirect(WriteTxn *txn, size_t count, blk_t* barray, bool* dirty);
    // Shrink |count| indirect blocks from the |iarray| array of indirect blocks. Sets |*dirty| to
    // true if anything is deleted.
    //
    // For the first indirect block in a set, only remove the blocks from index |bindex| up to the
    // end of the  indirect block. Only deletes this first indirect block if |bindex| is zero.
    //
    // On Fuchsia |ib_vmo_offset| contains the block offset of the |iarray| buffer within the
    // cached indirect VMO. On other platforms, this argument may be ignored.
    zx_status_t BlocksShrinkIndirect(WriteTxn* txn, uint32_t bindex, size_t count,
                                     uint32_t ib_vmo_offset, blk_t* iarray, bool* dirty);
    // Shrink |count| doubly indirect blocks from the |diarray| array of doubly indirect blocks.
    // Sets |*dirty| to true if anything is deleted.
    //
    // For the first doubly indirect block in a set, only remove blocks from indirect blocks from
    // index |ibindex| up to the end of the doubly indirect block. |bindex| is the first direct
    // block to be deleted in the first indirect block of the first doubly indirect block. Only
    // delete the doubly indirect block if |ibindex| is zero AND |bindex| is zero.
    //
    // On Fuchsia |dib_vmo_offset| contains the block offset of the |diarray| buffer within the
    // cached indirect VMO, and |ib_vmo_offset| contains the block offset of the indirect blocks
    // pointed to from the doubly indirect block at diarray[0]. On other platforms, this argument
    // may be ignored.
    zx_status_t BlocksShrinkDoublyIndirect(WriteTxn *txn, uint32_t ibindex, uint32_t bindex,
                                           size_t count, uint32_t dib_vmo_offset,
                                           uint32_t ib_vmo_offset, blk_t* diarray, bool* dirty);

#ifdef __Fuchsia__
    // Reads the block at |offset| in memory
    void ReadIndirectVmoBlock(uint32_t offset, uint32_t** entry);
    // Clears the block at |offset| in memory
    void ClearIndirectVmoBlock(uint32_t offset);
#else
    // Reads the block at |bno| on disk
    void ReadIndirectBlock(blk_t bno, uint32_t* entry);
    // Clears the block at |bno| on disk
    void ClearIndirectBlock(blk_t bno);
#endif

    // Update the vnode's inode and write it to disk
    void InodeSync(WriteTxn* txn, uint32_t flags);

    using DirentCallback = zx_status_t (*)(fbl::RefPtr<VnodeMinfs>,
                                           minfs_dirent_t*, DirArgs*,
                                           DirectoryOffset*);

    // Directories only
    zx_status_t ForEachDirent(DirArgs* args, const DirentCallback func);

#ifdef __Fuchsia__
    // The following functionality interacts with handles directly, and are not applicable outside
    // Fuchsia (since there is no "handle-equivalent" in host-side tools).

    zx_status_t VmoReadExact(void* data, uint64_t offset, size_t len);
    zx_status_t VmoWriteExact(const void* data, uint64_t offset, size_t len);

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
    fbl::unique_ptr<MappedVmo> vmo_indirect_{};

    vmoid_t vmoid_{};
    vmoid_t vmoid_indirect_{};

    // Use the watcher container to implement a directory watcher
    void Notify(fbl::StringPiece name, unsigned event) final;
    zx_status_t WatchDir(fs::Vfs* vfs, const vfs_watch_dir_t* cmd) final;

    // The vnode is acting as a mount point for a remote filesystem or device.
    virtual bool IsRemote() const final;
    virtual zx::channel DetachRemote() final;
    virtual zx_handle_t GetRemote() const final;
    virtual void SetRemote(zx::channel remote) final;

    fs::RemoteContainer remoter_{};
    fs::WatcherContainer watcher_{};
#endif
    uint32_t flags_{};
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

int minfs_mkfs(fbl::unique_ptr<Bcache> bc);

class MinfsChecker {
public:
    MinfsChecker();
    zx_status_t Init(fbl::unique_ptr<Bcache> bc, const minfs_info_t* info);
    zx_status_t CheckInode(ino_t ino, ino_t parent, bool dot_or_dotdot);
    zx_status_t CheckForUnusedBlocks() const;
    zx_status_t CheckForUnusedInodes() const;
    zx_status_t CheckLinkCounts() const;
    zx_status_t CheckAllocatedCounts() const;

    // "Set once"-style flag to identify if anything nonconforming
    // was found in the underlying filesystem -- even if it was fixed.
    bool conforming_;

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(MinfsChecker);

    zx_status_t GetInode(minfs_inode_t* inode, ino_t ino);

    // Returns the nth block within an inode, relative to the start of the
    // file. Returns the "next_n" which might contain a bno. This "next_n"
    // is for performance reasons -- it allows fsck to avoid repeatedly checking
    // the same indirect / doubly indirect blocks with all internal
    // bno unallocated.
    zx_status_t GetInodeNthBno(minfs_inode_t* inode, blk_t n, blk_t* next_n,
                               blk_t* bno_out);
    zx_status_t CheckDirectory(minfs_inode_t* inode, ino_t ino,
                               ino_t parent, uint32_t flags);
    const char* CheckDataBlock(blk_t bno);
    zx_status_t CheckFile(minfs_inode_t* inode, ino_t ino);

    fbl::unique_ptr<Minfs> fs_;
    RawBitmap checked_inodes_;
    RawBitmap checked_blocks_;

    uint32_t alloc_inodes_;
    uint32_t alloc_blocks_;
    fbl::Array<int32_t> links_;

    blk_t cached_doubly_indirect_;
    blk_t cached_indirect_;
    uint8_t doubly_indirect_cache_[kMinfsBlockSize];
    uint8_t indirect_cache_[kMinfsBlockSize];
};

zx_status_t minfs_check(fbl::unique_ptr<Bcache> bc);

zx_status_t minfs_mount(fbl::RefPtr<VnodeMinfs>* root_out, fbl::unique_ptr<Bcache> bc);

} // namespace minfs
