// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifdef __Fuchsia__
#include <fs/dispatcher.h>
#include <mx/vmo.h>
#endif

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

#include "block-txn.h"
#include "minfs.h"
#include "misc.h"

#define panic(fmt...)         \
    do {                      \
        fprintf(stderr, fmt); \
        __builtin_trap();     \
    } while (0)

namespace minfs {

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
    static mx_status_t Create(Minfs** out, mxtl::unique_ptr<Bcache> bc, const minfs_info_t* info);

    mx_status_t Unmount();

    // instantiate a vnode from an inode
    // the inode must exist in the file system
    mx_status_t VnodeGet(mxtl::RefPtr<VnodeMinfs>* out, uint32_t ino);

    // instantiate a vnode with a new inode
    mx_status_t VnodeNew(WriteTxn* txn, mxtl::RefPtr<VnodeMinfs>* out, uint32_t type);

    // remove a vnode from the hash map
    void VnodeRelease(VnodeMinfs* vn);

    // Allocate a new data block.
    mx_status_t BlockNew(WriteTxn* txn, uint32_t hint, uint32_t* out_bno);

    // free ino in inode bitmap, release all blocks held by inode
    mx_status_t InoFree(
#ifdef __Fuchsia__
        const MappedVmo* vmo_indirect,
#endif
        const minfs_inode_t& inode, uint32_t ino);

    // Writes back an inode into the inode table on persistent storage.
    // Does not modify inode bitmap.
    mx_status_t InodeSync(WriteTxn* txn, uint32_t ino, const minfs_inode_t* inode);

#ifdef __Fuchsia__
    fs::Dispatcher* GetDispatcher() {
        return dispatcher_.get();
    }
#endif

    void ValidateBno(uint32_t bno) const {
        MX_DEBUG_ASSERT(info_.dat_block <= bno);
        MX_DEBUG_ASSERT(bno < info_.block_count);
    }

    mxtl::unique_ptr<Bcache> bc_;
    RawBitmap block_map_;
#ifdef __Fuchsia__
    vmoid_t block_map_vmoid_;
#endif
    minfs_info_t info_;

private:
    // Fsck can introspect Minfs
    friend class MinfsChecker;
    Minfs(mxtl::unique_ptr<Bcache> bc_, const minfs_info_t* info_);
    // Find a free inode, allocate it in the inode bitmap, and write it back to disk
    mx_status_t InoNew(WriteTxn* txn, const minfs_inode_t* inode, uint32_t* ino_out);

#ifdef __Fuchsia__
    mxtl::unique_ptr<fs::Dispatcher> dispatcher_;
#endif
    uint32_t abmblks_;
    uint32_t ibmblks_;
    RawBitmap inode_map_;
#ifdef __Fuchsia__
    mxtl::unique_ptr<MappedVmo> inode_table_;
    vmoid_t inode_map_vmoid_;
    vmoid_t inode_table_vmoid_;
#endif
    // Vnodes exist in the hash table as long as one or more reference exists;
    // when the Vnode is deleted, it is immediately removed from the map.
    using HashTable = mxtl::HashTable<uint32_t, VnodeMinfs*>;
    HashTable vnode_hash_;
};

struct DirArgs {
    const char* name;
    size_t len;
    uint32_t ino;
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
constexpr uint32_t kMinfsFlagDeletedDirectory = 0x00010000;
constexpr uint32_t kMinfsFlagReservedMask     = 0xFFFF0000;
// clang-format on

static_assert((kMinfsFlagReservedMask & V_FLAG_RESERVED_MASK) == 0,
              "MinFS should not be using any Vnode flags which are reserved");

class VnodeMinfs final : public fs::Vnode, public mxtl::SinglyLinkedListable<VnodeMinfs*> {
public:
    // Allocates a Vnode and initializes the inode given the type.
    static mx_status_t Allocate(Minfs* fs, uint32_t type, mxtl::RefPtr<VnodeMinfs>* out);
    // Allocates a Vnode, but leaves the inode untouched, so it may be overwritten.
    static mx_status_t AllocateHollow(Minfs* fs, mxtl::RefPtr<VnodeMinfs>* out);

    bool IsDirectory() const { return inode_.magic == kMinfsMagicDir; }
    bool IsDeletedDirectory() const { return flags_ & kMinfsFlagDeletedDirectory; }
    bool CanUnlink() const;

    uint32_t GetKey() const { return ino_; }
    static size_t GetHash(uint32_t key) { return INO_HASH(key); }

    mx_status_t UnlinkChild(WriteTxn* txn, mxtl::RefPtr<VnodeMinfs> child,
                            minfs_dirent_t* de, DirectoryOffset* offs);
    // Remove the link to a vnode (referring to inodes exclusively).
    // Has no impact on direntries (or parent inode).
    void RemoveInodeLink(WriteTxn* txn);
    mx_status_t ReadInternal(void* data, size_t len, size_t off, size_t* actual);
    mx_status_t ReadExactInternal(void* data, size_t len, size_t off);
    mx_status_t WriteInternal(WriteTxn* txn, const void* data, size_t len,
                              size_t off, size_t* actual);
    mx_status_t WriteExactInternal(WriteTxn* txn, const void* data, size_t len,
                                   size_t off);
    mx_status_t TruncateInternal(WriteTxn* txn, size_t len);
    ssize_t Ioctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                  size_t out_len) final;
    mx_status_t Lookup(mxtl::RefPtr<fs::Vnode>* out, const char* name, size_t len) final;
    // Lookup which can traverse '..'
    mx_status_t LookupInternal(mxtl::RefPtr<fs::Vnode>* out, const char* name, size_t len);

    Minfs* fs_;
    uint32_t ino_;
    minfs_inode_t inode_;

    ~VnodeMinfs();

private:
    // Fsck can introspect Minfs
    friend class MinfsChecker;
    VnodeMinfs(Minfs* fs);

    // Implementing methods from the fs::Vnode, so MinFS vnodes may be utilized
    // by the VFS library.
    mx_status_t Open(uint32_t flags) final;
    ssize_t Read(void* data, size_t len, size_t off) final;
    ssize_t Write(const void* data, size_t len, size_t off) final;
    mx_status_t Getattr(vnattr_t* a) final;
    mx_status_t Setattr(vnattr_t* a) final;
    mx_status_t Readdir(void* cookie, void* dirents, size_t len) final;
    mx_status_t Create(mxtl::RefPtr<fs::Vnode>* out, const char* name, size_t len, uint32_t mode) final;
    mx_status_t Unlink(const char* name, size_t len, bool must_be_dir) final;
    mx_status_t Rename(mxtl::RefPtr<fs::Vnode> newdir,
                       const char* oldname, size_t oldlen,
                       const char* newname, size_t newlen,
                       bool src_must_be_dir, bool dst_must_be_dir) final;
    mx_status_t Link(const char* name, size_t len, mxtl::RefPtr<fs::Vnode> target) final;
    mx_status_t Truncate(size_t len) final;
    mx_status_t Sync() final;
    mx_status_t AttachRemote(mx_handle_t) final;

#ifdef __Fuchsia__
    mx_status_t InitVmo();
    mx_status_t InitIndirectVmo();
#endif

    // Get the disk block 'bno' corresponding to the 'nth' logical block of the file.
    // Allocate the block if requested with a non-null "txn".
    mx_status_t GetBno(WriteTxn* txn, uint32_t n, uint32_t* bno);

    // Deletes all blocks (relateive to a file) from "start" (inclusive) to the end
    // of the file. Does not update mtime/atime.
    mx_status_t BlocksShrink(WriteTxn* txn, uint32_t start);

    // Update the vnode's inode and write it to disk
    void InodeSync(WriteTxn* txn, uint32_t flags);

    using DirentCallback = mx_status_t (*)(mxtl::RefPtr<VnodeMinfs>,
                                           minfs_dirent_t*, DirArgs*,
                                           DirectoryOffset*);

    // Directories only
    mx_status_t ForEachDirent(DirArgs* args, const DirentCallback func);

#ifdef __Fuchsia__
    fs::Dispatcher* GetDispatcher() final;

    // The following functionality interacts with handles directly, and are not applicable outside
    // Fuchsia (since there is no "handle-equivalent" in host-side tools).

    mx_status_t VmoReadExact(void* data, uint64_t offset, size_t len);
    mx_status_t VmoWriteExact(const void* data, uint64_t offset, size_t len);

    // TODO(smklein): When we have can register MinFS as a pager service, and
    // it can properly handle pages faults on a vnode's contents, then we can
    // avoid reading the entire file up-front. Until then, read the contents of
    // a VMO into memory when it is read/written.
    mx::vmo vmo_;
    mxtl::unique_ptr<MappedVmo> vmo_indirect_;
    vmoid_t vmoid_;
    vmoid_t vmoid_indirect_;

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
void minfs_sync_vnode(mxtl::RefPtr<VnodeMinfs> vn, uint32_t flags);

mx_status_t minfs_check_info(const minfs_info_t* info, uint32_t max);
void minfs_dump_info(const minfs_info_t* info);
void minfs_dump_inode(const minfs_inode_t* inode, uint32_t ino);

int minfs_mkfs(mxtl::unique_ptr<Bcache> bc);

#ifdef __Fuchsia__

class MinfsChecker {
public:
    MinfsChecker();
    mx_status_t Init(mxtl::unique_ptr<Bcache> bc, const minfs_info_t* info);
    mx_status_t CheckInode(uint32_t ino, uint32_t parent, bool dot_or_dotdot);
    mx_status_t CheckForUnusedBlocks() const;
    mx_status_t CheckForUnusedInodes() const;
    mx_status_t CheckLinkCounts() const;

    // "Set once"-style flag to identify if anything nonconforming
    // was found in the underlying filesystem -- even if it was fixed.
    bool conforming_;

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(MinfsChecker);

    mx_status_t GetInode(minfs_inode_t* inode, uint32_t ino);
    mx_status_t GetInodeNthBno(minfs_inode_t* inode, uint32_t n, uint32_t* bno_out);
    mx_status_t CheckDirectory(minfs_inode_t* inode, uint32_t ino,
                               uint32_t parent, uint32_t flags);
    const char* CheckDataBlock(uint32_t bno);
    mx_status_t CheckFile(minfs_inode_t* inode, uint32_t ino);

    mxtl::unique_ptr<Minfs> fs_;
    RawBitmap checked_inodes_;
    RawBitmap checked_blocks_;

    mxtl::Array<int32_t> links_;
};

mx_status_t minfs_check(mxtl::unique_ptr<Bcache> bc);
#endif

mx_status_t minfs_mount(mxtl::RefPtr<VnodeMinfs>* root_out, mxtl::unique_ptr<Bcache> bc);

void minfs_dir_init(void* bdata, uint32_t ino_self, uint32_t ino_parent);

} // namespace minfs
