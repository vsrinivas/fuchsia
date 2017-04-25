// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mxtl/algorithm.h>
#include <mxtl/intrusive_hash_table.h>
#include <mxtl/intrusive_single_list.h>
#include <mxtl/macros.h>
#include <mxtl/ref_ptr.h>
#include <mxtl/unique_ptr.h>

#include <fs/mapped-vmo.h>
#ifdef __Fuchsia__
#include <fs/vfs-dispatcher.h>
#endif
#include <fs/vfs.h>

#include "minfs.h"
#include "misc.h"

#define panic(fmt...) do { fprintf(stderr, fmt); __builtin_trap(); } while (0)

namespace minfs {

// minfs_sync_vnode flags
constexpr uint32_t kMxFsSyncDefault = 0;     // default: no implicit time update
constexpr uint32_t kMxFsSyncMtime   = (1<<0);
constexpr uint32_t kMxFsSyncCtime   = (1<<1);

constexpr uint32_t kMinfsBlockCacheSize = 64;

// Used by fsck
struct CheckMaps {
    RawBitmap checked_inodes;
    RawBitmap checked_blocks;
};

class VnodeMinfs;

class Minfs {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Minfs);

    static mx_status_t Create(Minfs** out, Bcache* bc, minfs_info_t* info);

    mx_status_t Unmount();

    // instantiate a vnode from an inode
    // the inode must exist in the file system
    mx_status_t VnodeGet(VnodeMinfs** out, uint32_t ino);

    // instantiate a vnode with a new inode
    mx_status_t VnodeNew(VnodeMinfs** out, uint32_t type);

    // remove a vnode from the hash map
    void VnodeRelease(VnodeMinfs* vn);

    // Allocate a new data block and bcache_get_zero() it.
    // Acquires the block if out_block is not null.
    mx_status_t BlockNew(uint32_t hint, uint32_t* out_bno, mxtl::RefPtr<BlockNode>* out_block);

    // free ino in inode bitmap, release all blocks held by inode
    mx_status_t InoFree(const minfs_inode_t& inode, uint32_t ino);

    // Writes back an inode into the inode table on persistent storage.
    // Does not modify inode bitmap.
    mx_status_t InodeSync(uint32_t ino, const minfs_inode_t* inode);

    // When modifying bit 'n' in the bitmap, the following pattern may be used:
    //   blk = nullptr;
    //   blk = BitmapBlockGet(blk, n);
    //   block_map.Access(n);
    //   BitmapBlockPut(blk);
    // To guarantee the in-memory bitmap is in sync with the on-disk bitmap. Repeated
    // access to related (nearby) bits in the bitmap will defer writing to the disk
    // until "BitmapBlockPut" is called.
    mxtl::RefPtr<BlockNode> BitmapBlockGet(const mxtl::RefPtr<BlockNode>& blk, uint32_t n);
    void BitmapBlockPut(const mxtl::RefPtr<BlockNode>& blk);

    Bcache* bc_;
    RawBitmap block_map_;
    minfs_info_t info_;
#ifdef __Fuchsia__
    mxtl::unique_ptr<fs::VfsDispatcher> dispatcher_;
#endif

private:
    // Fsck can introspect Minfs
    friend mx_status_t check_inode(CheckMaps*, const Minfs*, uint32_t, uint32_t);
    friend mx_status_t minfs_check(Bcache*);
    Minfs(Bcache* bc_, minfs_info_t* info_);
    // Find a free inode, allocate it in the inode bitmap, and write it back to disk
    mx_status_t InoNew(const minfs_inode_t* inode, uint32_t* ino_out);
    mx_status_t LoadBitmaps();

    uint32_t abmblks_;
    uint32_t ibmblks_;
    RawBitmap inode_map_;
#ifdef __Fuchsia__
    mxtl::unique_ptr<MappedVmo> inode_table_;
#endif
    using HashTable = mxtl::HashTable<uint32_t, VnodeMinfs*>;
    HashTable vnode_hash_;
};

struct DirArgs {
    const char* name;
    size_t len;
    uint32_t ino;
    uint32_t type;
    uint32_t reclen;
};

struct DirectoryOffset {
    size_t off;      // Offset in directory of current record
    size_t off_prev; // Offset in directory of previous record
};

#define INO_HASH(ino) fnv1a_tiny(ino, kMinfsHashBits)

constexpr uint32_t kMinfsFlagDeletedDirectory = 0x00010000;
constexpr uint32_t kMinfsFlagReservedMask     = 0xFFFF0000;

static_assert((kMinfsFlagReservedMask & V_FLAG_RESERVED_MASK) == 0,
              "MinFS should not be using any Vnode flags which are reserved");

class VnodeMinfs final : public fs::Vnode, public mxtl::SinglyLinkedListable<VnodeMinfs*> {
    friend class Minfs;
public:
    // Allocates a Vnode and initializes the inode given the type.
    static mx_status_t Allocate(Minfs* fs, uint32_t type, VnodeMinfs** out);
    // Allocates a Vnode, but leaves the inode untouched, so it may be overwritten.
    static mx_status_t AllocateHollow(Minfs* fs, VnodeMinfs** out);

    bool IsDirectory() const { return inode_.magic == kMinfsMagicDir; }
    bool IsDeletedDirectory() const { return flags_ & kMinfsFlagDeletedDirectory; }
    bool CanUnlink() const;

    uint32_t GetKey() const { return ino_; }
    static size_t GetHash(uint32_t key) { return INO_HASH(key); }

    mx_status_t UnlinkChild(VnodeMinfs* child, minfs_dirent_t* de, DirectoryOffset* offs);
    mx_status_t ReadInternal(void* data, size_t len, size_t off, size_t* actual);
    mx_status_t ReadExactInternal(void* data, size_t len, size_t off);
    mx_status_t WriteInternal(const void* data, size_t len, size_t off, size_t* actual);
    mx_status_t WriteExactInternal(const void* data, size_t len, size_t off);
    mx_status_t TruncateInternal(size_t len);
    ssize_t Ioctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                  size_t out_len) final;
    mx_status_t Lookup(fs::Vnode** out, const char* name, size_t len) final;
    // Lookup which can traverse '..'
    mx_status_t LookupInternal(fs::Vnode** out, const char* name, size_t len);

    Minfs* fs_;
    uint32_t ino_;
    minfs_inode_t inode_;

private:
    VnodeMinfs(Minfs* fs);

    // Implementing methods from the fs::Vnode, so MinFS vnodes may be utilized
    // by the VFS library.
    void Release() final;
    mx_status_t Open(uint32_t flags) final;
    mx_status_t Close() final;
    ssize_t Read(void* data, size_t len, size_t off) final;
    ssize_t Write(const void* data, size_t len, size_t off) final;
    mx_status_t Getattr(vnattr_t* a) final;
    mx_status_t Setattr(vnattr_t* a) final;
    mx_status_t Readdir(void* cookie, void* dirents, size_t len) final;
    mx_status_t Create(fs::Vnode** out, const char* name, size_t len, uint32_t mode) final;
    mx_status_t Unlink(const char* name, size_t len, bool must_be_dir) final;
    mx_status_t Rename(fs::Vnode* newdir,
                       const char* oldname, size_t oldlen,
                       const char* newname, size_t newlen,
                       bool src_must_be_dir, bool dst_must_be_dir) final;
    mx_status_t Link(const char* name, size_t len, fs::Vnode* target) final;
    mx_status_t Truncate(size_t len) final;
    mx_status_t Sync() final;
    mx_status_t AttachRemote(mx_handle_t) final;

    mx_status_t InitVmo();

    // Read data from disk at block 'bno', into the 'nth' logical block of the file.
    mx_status_t FillBlock(uint32_t n, uint32_t bno);

    // Get the disk block 'bno' corresponding to the 'nth' logical block of the file.
    // Allocate the block if reqeusted.
    mx_status_t GetBno(uint32_t n, uint32_t* bno, bool alloc);

    // Deletes all blocks (relateive to a file) from "start" (inclusive) to the end
    // of the file. Does not update mtime/atime.
    mx_status_t BlocksShrink(uint32_t start);

    // Update the vnode's inode and write it to disk
    void InodeSync(uint32_t flags);
    // Destroy the inode on disk (and free associated resources)
    mx_status_t InodeDestroy();

    // Directories only
    mx_status_t ForEachDirent(DirArgs* args,
                              mx_status_t (*func)(VnodeMinfs*, minfs_dirent_t*, DirArgs*,
                                                  DirectoryOffset*));

#ifdef __Fuchsia__
    mx_status_t AddDispatcher(mx_handle_t h, vfs_iostate_t* cookie) final;

    // The following functionality interacts with handles directly, and are not applicable outside
    // Fuchsia (since there is no "handle-equivalent" in host-side tools).

    // TODO(smklein): When we have can register MinFS as a pager service, and
    // it can properly handle pages faults on a vnode's contents, then we can
    // avoid reading the entire file up-front. Until then, read the contents of
    // a VMO into memory when it is read/written.
    mx_handle_t vmo_;

#endif
    // The vnode is acting as a mount point for a remote filesystem or device.
    virtual bool IsRemote() const final;
    virtual mx_handle_t DetachRemote() final;
    virtual mx_handle_t WaitForRemote() final;
    virtual mx_handle_t GetRemote() const final;
    virtual void SetRemote(mx_handle_t remote) final;
    fs::RemoteContainer remoter_;
};

// write the inode data of this vnode to disk (default does not update time values)
void minfs_sync_vnode(VnodeMinfs* vn, uint32_t flags);

mx_status_t minfs_check_info(minfs_info_t* info, uint32_t max);
void minfs_dump_info(minfs_info_t* info);

int minfs_mkfs(Bcache* bc);

mx_status_t check_inode(CheckMaps*, const Minfs*, uint32_t, uint32_t);
mx_status_t minfs_check(Bcache* bc);

mx_status_t minfs_mount(VnodeMinfs** root_out, Bcache* bc);

void minfs_dir_init(void* bdata, uint32_t ino_self, uint32_t ino_parent);

} // namespace minfs
