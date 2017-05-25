// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#include <mxtl/algorithm.h>
#include <magenta/device/vfs.h>

#ifdef __Fuchsia__
#include <magenta/syscalls.h>
#include <mxio/vfs.h>
#endif

#include "minfs-private.h"
#include "block-txn.h"

namespace {

mx_time_t minfs_gettime_utc() {
    // linux/magenta compatible
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    mx_time_t time = MX_SEC(ts.tv_sec)+ts.tv_nsec;
    return time;
}

} // namespace anonymous

namespace minfs {

#ifdef __Fuchsia__
mx_status_t VnodeMinfs::VmoReadExact(void* data, uint64_t offset, size_t len) {
    size_t actual;
    mx_status_t status = vmo_.read(data, offset, len, &actual);
    if (status != NO_ERROR) {
        return status;
    } else if (actual != len) {
        return ERR_IO;
    }
    return NO_ERROR;
}

mx_status_t VnodeMinfs::VmoWriteExact(const void* data, uint64_t offset, size_t len) {
    size_t actual;
    mx_status_t status = vmo_.write(data, offset, len, &actual);
    if (status != NO_ERROR) {
        return status;
    } else if (actual != len) {
        return ERR_IO;
    }
    return NO_ERROR;
}
#endif


void VnodeMinfs::InodeSync(WriteTxn* txn, uint32_t flags) {
    // by default, c/mtimes are not updated to current time
    if (flags != kMxFsSyncDefault) {
        mx_time_t cur_time = minfs_gettime_utc();
        // update times before syncing
        if ((flags & kMxFsSyncMtime) != 0) {
            inode_.modify_time = cur_time;
        }
        if ((flags & kMxFsSyncCtime) != 0) {
            inode_.create_time = cur_time;
        }
    }

    fs_->InodeSync(txn, ino_, &inode_);
}

// Delete all blocks (relative to a file) from "start" (inclusive) to the end of
// the file. Does not update mtime/atime.
mx_status_t VnodeMinfs::BlocksShrink(WriteTxn *txn, uint32_t start) {
#ifdef __Fuchsia__
    auto bbm_id = fs_->block_map_vmoid_;
#else
    auto bbm_id = fs_->block_map_.StorageUnsafe()->GetData();
#endif

    bool doSync = false;

    // release direct blocks
    for (unsigned bno = start; bno < kMinfsDirect; bno++) {
        if (inode_.dnum[bno] == 0) {
            continue;
        }
        fs_->ValidateBno(inode_.dnum[bno]);

        fs_->block_map_.Clear(inode_.dnum[bno], inode_.dnum[bno] + 1);
        uint32_t bitblock = inode_.dnum[bno] / kMinfsBlockBits;
        txn->Enqueue(bbm_id, bitblock, fs_->info_.abm_block + bitblock, 1);
        inode_.dnum[bno] = 0;
        inode_.block_count--;
        doSync = true;
    }

    const unsigned direct_per_indirect = kMinfsBlockSize / sizeof(uint32_t);

    // release indirect blocks
    for (unsigned indirect = 0; indirect < kMinfsIndirect; indirect++) {
        if (inode_.inum[indirect] == 0) {
            continue;
        }
        fs_->ValidateBno(inode_.inum[indirect]);
        unsigned bno = kMinfsDirect + (indirect + 1) * direct_per_indirect;
        if (start > bno) {
            continue;
        }
#ifdef __Fuchsia__
        MX_DEBUG_ASSERT(vmo_indirect_ != nullptr);
        uintptr_t iaddr = reinterpret_cast<uintptr_t>(vmo_indirect_->GetData());
        uint32_t* entry = reinterpret_cast<uint32_t*>(iaddr + kMinfsBlockSize * indirect);
#else
        uint8_t idata[kMinfsBlockSize];
        fs_->bc_->Readblk(inode_.inum[indirect], idata);
        uint32_t* entry = reinterpret_cast<uint32_t*>(idata);
#endif
        bool dirty = false;
        bool delete_indirect = true; // can we delete the indirect block?
        // release the blocks pointed at by the entries in the indirect block
        for (unsigned direct = 0; direct < direct_per_indirect; direct++) {
            if (entry[direct] == 0) {
                continue;
            }
            fs_->ValidateBno(entry[direct]);
            unsigned bno = kMinfsDirect + indirect * direct_per_indirect + direct;
            if (start > bno) {
                // This is a valid entry which exists in the indirect block
                // BEFORE our truncation point. Don't delete it, and don't
                // delete the indirect block.
                delete_indirect = false;
                continue;
            }

            fs_->block_map_.Clear(entry[direct], entry[direct] + 1);
            uint32_t bitblock = entry[direct] / kMinfsBlockBits;
            txn->Enqueue(bbm_id, bitblock, fs_->info_.abm_block + bitblock, 1);
            entry[direct] = 0;
            dirty = true;
            inode_.block_count--;
        }
        // only update the indirect block if an entry was deleted
        if (dirty) {
            doSync = true;
#ifdef __Fuchsia__
            txn->Enqueue(vmoid_indirect_, indirect, inode_.inum[indirect], 1);
#else
            fs_->bc_->Writeblk(inode_.inum[indirect], entry);
#endif
        }

        if (delete_indirect)  {
            // release the direct block itself
            fs_->block_map_.Clear(inode_.inum[indirect], inode_.inum[indirect] + 1);
            uint32_t bitblock = inode_.inum[indirect] / kMinfsBlockBits;
            txn->Enqueue(bbm_id, bitblock, fs_->info_.abm_block + bitblock, 1);
            inode_.inum[indirect] = 0;
            inode_.block_count--;
            doSync = true;
        }
    }

    if (doSync) {
        InodeSync(txn, kMxFsSyncDefault);
    }
    return NO_ERROR;
}

#ifdef __Fuchsia__

mx_status_t VnodeMinfs::InitIndirectVmo() {
    if (vmo_indirect_ != nullptr) {
        return NO_ERROR;
    }

    constexpr size_t size = kMinfsBlockSize * kMinfsIndirect;
    mx_status_t status;
    if ((status = MappedVmo::Create(size, &vmo_indirect_)) != NO_ERROR) {
        return status;
    }
    if ((status = fs_->bc_->AttachVmo(vmo_indirect_->GetVmo(),
                                      &vmoid_indirect_)) != NO_ERROR) {
        vmo_indirect_ = nullptr;
        return status;
    }

    ReadTxn txn(fs_->bc_.get());
    for (uint32_t i = 0; i < kMinfsIndirect; i++) {
        uint32_t ibno;
        if ((ibno = inode_.inum[i]) != 0) {
            fs_->ValidateBno(ibno);
            txn.Enqueue(vmoid_indirect_, i, ibno, 1);
        }
    }
    return txn.Flush();
}

// Since we cannot yet register the filesystem as a paging service (and cleanly
// fault on pages when they are actually needed), we currently read an entire
// file to a VMO when a file's data block are accessed.
//
// TODO(smklein): Even this hack can be optimized; a bitmap could be used to
// track all 'empty/read/dirty' blocks for each vnode, rather than reading
// the entire file.
mx_status_t VnodeMinfs::InitVmo() {
    if (vmo_.is_valid()) {
        return NO_ERROR;
    }

    mx_status_t status;
    if ((status = mx::vmo::create(mxtl::roundup(inode_.size, kMinfsBlockSize),
                                  0, &vmo_)) != NO_ERROR) {
        error("Failed to initialize vmo; error: %d\n", status);
        return status;
    }

    if ((status = fs_->bc_->AttachVmo(vmo_.get(), &vmoid_)) != NO_ERROR) {
        vmo_.reset();
        return status;
    }
    ReadTxn txn(fs_->bc_.get());

    // Initialize all direct blocks
    uint32_t bno;
    for (uint32_t d = 0; d < kMinfsDirect; d++) {
        if ((bno = inode_.dnum[d]) != 0) {
            fs_->ValidateBno(bno);
            txn.Enqueue(vmoid_, d, bno, 1);
        }
    }

    // Initialize all indirect blocks
    for (uint32_t i = 0; i < kMinfsIndirect; i++) {
        uint32_t ibno;
        if ((ibno = inode_.inum[i]) != 0) {
            fs_->ValidateBno(ibno);

            // Only initialize the indirect vmo if it is being used.
            if ((status = InitIndirectVmo()) != NO_ERROR) {
                vmo_.reset();
                return status;
            }

            MX_DEBUG_ASSERT(vmo_indirect_ != nullptr);
            uintptr_t iaddr = reinterpret_cast<uintptr_t>(vmo_indirect_->GetData());
            uint32_t* ientry = reinterpret_cast<uint32_t*>(iaddr + kMinfsBlockSize * i);

            constexpr uint32_t direct_per_indirect = kMinfsBlockSize / sizeof(uint32_t);
            for (uint32_t j = 0; j < direct_per_indirect; j++) {
                if ((bno = ientry[j]) != 0) {
                    fs_->ValidateBno(bno);
                    uint32_t n = kMinfsDirect + i * direct_per_indirect + j;
                    txn.Enqueue(vmoid_, n, bno, 1);
                }
            }
        }
    }

    return txn.Flush();
}
#endif

// Get the bno corresponding to the nth logical block within the file.
mx_status_t VnodeMinfs::GetBno(WriteTxn* txn, uint32_t n, uint32_t* bno) {
    uint32_t hint = 0;
    // direct blocks are simple... is there an entry in dnum[]?
    if (n < kMinfsDirect) {
        if (((*bno = inode_.dnum[n]) == 0) && (txn != nullptr)) {
            mx_status_t status = fs_->BlockNew(txn, hint, bno);
            if (status != NO_ERROR) {
                return status;
            }
            inode_.dnum[n] = *bno;
            inode_.block_count++;
        }
        fs_->ValidateBno(*bno);
        return NO_ERROR;
    }

    // for indirect blocks, adjust past the direct blocks
    n -= kMinfsDirect;

    // determine indices into the indirect block list and into
    // the block list in the indirect block
    uint32_t i = static_cast<uint32_t>(n / (kMinfsBlockSize / sizeof(uint32_t)));
    uint32_t j = n % (kMinfsBlockSize / sizeof(uint32_t));

    if (i >= kMinfsIndirect) {
        return ERR_OUT_OF_RANGE;
    }

    mx_status_t status;
#ifdef __Fuchsia__
    // If the vmo_indirect_ vmo has not been created, make it now.
    if ((status = InitIndirectVmo()) != NO_ERROR) {
        return status;
    }
#else
    uint8_t idata[kMinfsBlockSize];
#endif

    uint32_t ibno;
    bool dirty = false;

    // look up the indirect bno
    if ((ibno = inode_.inum[i]) == 0) {
        if (txn == nullptr) {
            *bno = 0;
            return NO_ERROR;
        }
        // allocate a new indirect block
        if ((status = fs_->BlockNew(txn, hint, &ibno)) != NO_ERROR) {
            return status;
        }
#ifdef __Fuchsia__
        MX_DEBUG_ASSERT(vmo_indirect_ != nullptr);
        uintptr_t iaddr = reinterpret_cast<uintptr_t>(vmo_indirect_->GetData());
        memset(reinterpret_cast<void*>(iaddr + kMinfsBlockSize * i), 0, kMinfsBlockSize);
#else
        memset(idata, 0, kMinfsBlockSize);
        fs_->bc_->Writeblk(ibno, idata);
#endif

        // record new indirect block in inode, note that we need to update
        inode_.block_count++;
        inode_.inum[i] = ibno;
        dirty = true;
    }
#ifdef __Fuchsia__
    MX_DEBUG_ASSERT(vmo_indirect_ != nullptr);
    uintptr_t iaddr = reinterpret_cast<uintptr_t>(vmo_indirect_->GetData());
    uint32_t* ientry = reinterpret_cast<uint32_t*>(iaddr + kMinfsBlockSize * i);
#else
    fs_->bc_->Readblk(ibno, idata);
    uint32_t* ientry = reinterpret_cast<uint32_t*>(idata);
#endif

    if (((*bno = ientry[j]) == 0) && (txn != nullptr)) {
        // allocate a new block
        status = fs_->BlockNew(txn, hint, bno);
        if (status != NO_ERROR) {
            return status;
        }
        inode_.block_count++;
        ientry[j] = *bno;
        dirty = true;
    }

    if (dirty) {
        // Write back the indirect block if requested
#ifdef __Fuchsia__
        txn->Enqueue(vmoid_indirect_, i, ibno, 1);
#else
        fs_->bc_->Writeblk(ibno, ientry);
#endif
        InodeSync(txn, kMxFsSyncDefault);
    }

    fs_->ValidateBno(*bno);
    return NO_ERROR;
}

// Immediately stop iterating over the directory.
#define DIR_CB_DONE 0
// Access the next direntry in the directory. Offsets updated.
#define DIR_CB_NEXT 1
// Identify that the direntry record was modified. Stop iterating.
#define DIR_CB_SAVE_SYNC 2

mx_status_t VnodeMinfs::ReadExactInternal(void* data, size_t len, size_t off) {
    size_t actual;
    mx_status_t status = ReadInternal(data, len, off, &actual);
    if (status != NO_ERROR) {
        return status;
    } else if (actual != len) {
        return ERR_IO;
    }
    return NO_ERROR;
}

mx_status_t VnodeMinfs::WriteExactInternal(WriteTxn* txn, const void* data,
                                           size_t len, size_t off) {
    size_t actual;
    mx_status_t status = WriteInternal(txn, data, len, off, &actual);
    if (status != NO_ERROR) {
        return status;
    } else if (actual != len) {
        return ERR_IO;
    }
    InodeSync(txn, kMxFsSyncMtime);
    return NO_ERROR;
}

static mx_status_t validate_dirent(minfs_dirent_t* de, size_t bytes_read, size_t off) {
    uint32_t reclen = static_cast<uint32_t>(MinfsReclen(de, off));
    if ((bytes_read < MINFS_DIRENT_SIZE) || (reclen < MINFS_DIRENT_SIZE)) {
        error("vn_dir: Could not read dirent at offset: %zd\n", off);
        return ERR_IO;
    } else if ((off + reclen > kMinfsMaxDirectorySize) || (reclen & 3)) {
        error("vn_dir: bad reclen %u > %u\n", reclen, kMinfsMaxDirectorySize);
        return ERR_IO;
    } else if (de->ino != 0) {
        if ((de->namelen == 0) ||
            (de->namelen > (reclen - MINFS_DIRENT_SIZE))) {
            error("vn_dir: bad namelen %u / %u\n", de->namelen, reclen);
            return ERR_IO;
        }
    }
    return NO_ERROR;
}

// Updates offset information to move to the next direntry in the directory.
static mx_status_t do_next_dirent(minfs_dirent_t* de, DirectoryOffset* offs) {
    offs->off_prev = offs->off;
    offs->off += MinfsReclen(de, offs->off);
    return DIR_CB_NEXT;
}

static mx_status_t cb_dir_find(mxtl::RefPtr<VnodeMinfs> vndir, minfs_dirent_t* de,
                               DirArgs* args, DirectoryOffset* offs) {
    if ((de->ino != 0) && (de->namelen == args->len) &&
        (!memcmp(de->name, args->name, args->len))) {
        args->ino = de->ino;
        args->type = de->type;
        return DIR_CB_DONE;
    } else {
        return do_next_dirent(de, offs);
    }
}

bool VnodeMinfs::CanUnlink() const {
    // directories must be empty (dirent_count == 2)
    if (IsDirectory()) {
        if (inode_.dirent_count != 2) {
            // if we have more than "." and "..", not empty, cannot unlink
            return false;
        }
    }
    return true;
}

mx_status_t VnodeMinfs::UnlinkChild(WriteTxn* txn,
                                    mxtl::RefPtr<VnodeMinfs> childvn,
                                    minfs_dirent_t* de, DirectoryOffset* offs) {
    // Coalesce the current dirent with the previous/next dirent, if they
    // (1) exist and (2) are free.
    size_t off_prev = offs->off_prev;
    size_t off = offs->off;
    size_t off_next = off + MinfsReclen(de, off);
    minfs_dirent_t de_prev, de_next;
    mx_status_t status;

    // Read the direntries we're considering merging with.
    // Verify they are free and small enough to merge.
    size_t coalesced_size = MinfsReclen(de, off);
    // Coalesce with "next" first, so the kMinfsReclenLast bit can easily flow
    // back to "de" and "de_prev".
    if (!(de->reclen & kMinfsReclenLast)) {
        size_t len = MINFS_DIRENT_SIZE;
        if ((status = ReadExactInternal(&de_next, len, off_next)) != NO_ERROR) {
            error("unlink: Failed to read next dirent\n");
            return status;
        } else if ((status = validate_dirent(&de_next, len, off_next)) != NO_ERROR) {
            error("unlink: Read invalid dirent\n");
            return status;
        }
        if (de_next.ino == 0) {
            coalesced_size += MinfsReclen(&de_next, off_next);
            // If the next entry *was* last, then 'de' is now last.
            de->reclen |= (de_next.reclen & kMinfsReclenLast);
        }
    }
    if (off_prev != off) {
        size_t len = MINFS_DIRENT_SIZE;
        if ((status = ReadExactInternal(&de_prev, len, off_prev)) != NO_ERROR) {
            error("unlink: Failed to read previous dirent\n");
            return status;
        } else if ((status = validate_dirent(&de_prev, len, off_prev)) != NO_ERROR) {
            error("unlink: Read invalid dirent\n");
            return status;
        }
        if (de_prev.ino == 0) {
            coalesced_size += MinfsReclen(&de_prev, off_prev);
            off = off_prev;
        }
    }

    if (!(de->reclen & kMinfsReclenLast) && (coalesced_size >= kMinfsReclenMask)) {
        // Should only be possible if the on-disk record format is corrupted
        return ERR_IO;
    }
    de->ino = 0;
    de->reclen = static_cast<uint32_t>(coalesced_size & kMinfsReclenMask) |
        (de->reclen & kMinfsReclenLast);
    // Erase dirent (replace with 'empty' dirent)
    if ((status = WriteExactInternal(txn, de, MINFS_DIRENT_SIZE, off)) != NO_ERROR) {
        return status;
    }

    if (de->reclen & kMinfsReclenLast) {
        // Truncating the directory merely removed unused space; if it fails,
        // the directory contents are still valid.
        TruncateInternal(txn, off + MINFS_DIRENT_SIZE);
    }

    inode_.dirent_count--;

    if (MinfsMagicType(childvn->inode_.magic) == kMinfsTypeDir) {
        // Child directory had '..' which pointed to parent directory
        inode_.link_count--;
    }
    childvn->RemoveInodeLink(txn);
    return DIR_CB_SAVE_SYNC;
}

void VnodeMinfs::RemoveInodeLink(WriteTxn* txn) {
    // This effectively 'unlinks' the target node without deleting the direntry
    inode_.link_count--;
    if (MinfsMagicType(inode_.magic) == kMinfsTypeDir) {
        if (inode_.link_count == 1) {
            // Directories are initialized with two links, since they point
            // to themselves via ".". Thus, when they reach "one link", they
            // are only pointed to by themselves, and should be deleted.
            inode_.link_count--;
            flags_ |= kMinfsFlagDeletedDirectory;
        }
    }

    InodeSync(txn, kMxFsSyncMtime);
}

// caller is expected to prevent unlink of "." or ".."
static mx_status_t cb_dir_unlink(mxtl::RefPtr<VnodeMinfs> vndir, minfs_dirent_t* de,
                                 DirArgs* args, DirectoryOffset* offs) {
    if ((de->ino == 0) || (args->len != de->namelen) ||
        memcmp(args->name, de->name, args->len)) {
        return do_next_dirent(de, offs);
    }

    mxtl::RefPtr<VnodeMinfs> vn;
    mx_status_t status;
    if ((status = vndir->fs_->VnodeGet(&vn, de->ino)) < 0) {
        return status;
    }

    // If a directory was requested, then only try unlinking a directory
    if ((args->type == kMinfsTypeDir) && !vn->IsDirectory()) {
        return ERR_NOT_DIR;
    }
    if (!vn->CanUnlink()) {
        return ERR_BAD_STATE;
    }
    return vndir->UnlinkChild(args->txn, mxtl::move(vn), de, offs);
}

// same as unlink, but do not validate vnode
static mx_status_t cb_dir_force_unlink(mxtl::RefPtr<VnodeMinfs> vndir, minfs_dirent_t* de,
                                       DirArgs* args, DirectoryOffset* offs) {
    if ((de->ino == 0) || (args->len != de->namelen) ||
        memcmp(args->name, de->name, args->len)) {
        return do_next_dirent(de, offs);
    }

    mxtl::RefPtr<VnodeMinfs> vn;
    mx_status_t status;
    if ((status = vndir->fs_->VnodeGet(&vn, de->ino)) < 0) {
        return status;
    }
    return vndir->UnlinkChild(args->txn, mxtl::move(vn), de, offs);
}

// Given a (name, inode, type) combination:
//   - If no corresponding 'name' is found, ERR_NOT_FOUND is returned
//   - If the 'name' corresponds to a vnode, check that the target vnode:
//      - Does not have the same inode as the argument inode
//      - Is the same type as the argument 'type'
//      - Is unlinkable
//   - If the previous checks pass, then:
//      - Remove the old vnode (decrement link count by one)
//      - Replace the old vnode's position in the directory with the new inode
static mx_status_t cb_dir_attempt_rename(mxtl::RefPtr<VnodeMinfs> vndir, minfs_dirent_t* de,
                                         DirArgs* args, DirectoryOffset* offs) {
    if ((de->ino == 0) || (args->len != de->namelen) ||
        memcmp(args->name, de->name, args->len)) {
        return do_next_dirent(de, offs);
    }

    mxtl::RefPtr<VnodeMinfs> vn;
    mx_status_t status;
    if ((status = vndir->fs_->VnodeGet(&vn, de->ino)) < 0) {
        return status;
    } else if (args->ino == vn->ino_) {
        // cannot rename node to itself
        return ERR_BAD_STATE;
    } else if (args->type != de->type) {
        // cannot rename directory to file (or vice versa)
        return ERR_BAD_STATE;
    } else if (!vn->CanUnlink()) {
        // if we cannot unlink the target, we cannot rename the target
        return ERR_BAD_STATE;
    }

    // If we are renaming ON TOP of a directory, then we can skip
    // updating the parent link count -- the old directory had a ".." entry to
    // the parent (link count of 1), but the new directory will ALSO have a ".."
    // entry, making the rename operation idempotent w.r.t. the parent link
    // count.
    vn->RemoveInodeLink(args->txn);

    de->ino = args->ino;
    status = vndir->WriteExactInternal(args->txn, de, DirentSize(de->namelen), offs->off);
    if (status != NO_ERROR) {
        return status;
    }
    return DIR_CB_SAVE_SYNC;
}

static mx_status_t cb_dir_update_inode(mxtl::RefPtr<VnodeMinfs> vndir, minfs_dirent_t* de,
                                       DirArgs* args, DirectoryOffset* offs) {
    if ((de->ino == 0) || (args->len != de->namelen) ||
        memcmp(args->name, de->name, args->len)) {
        return do_next_dirent(de, offs);
    }

    de->ino = args->ino;
    mx_status_t status = vndir->WriteExactInternal(args->txn, de,
                                                   DirentSize(de->namelen),
                                                   offs->off);
    if (status != NO_ERROR) {
        return status;
    }
    return DIR_CB_SAVE_SYNC;
}

static mx_status_t add_dirent(mxtl::RefPtr<VnodeMinfs> vndir,
                              minfs_dirent_t* de, DirArgs* args, size_t off) {
    de->ino = args->ino;
    de->type = static_cast<uint8_t>(args->type);
    de->namelen = static_cast<uint8_t>(args->len);
    memcpy(de->name, args->name, args->len);
    mx_status_t status = vndir->WriteExactInternal(args->txn, de,
                                                   DirentSize(de->namelen),
                                                   off);
    if (status != NO_ERROR) {
        return status;
    }
    vndir->inode_.dirent_count++;
    if (args->type == kMinfsTypeDir) {
        // Child directory has '..' which will point to parent directory
        vndir->inode_.link_count++;
    }
    return DIR_CB_SAVE_SYNC;
}

static mx_status_t cb_dir_append(mxtl::RefPtr<VnodeMinfs> vndir, minfs_dirent_t* de,
                                 DirArgs* args, DirectoryOffset* offs) {
    uint32_t reclen = static_cast<uint32_t>(MinfsReclen(de, offs->off));
    if (de->ino == 0) {
        // empty entry, do we fit?
        if (args->reclen > reclen) {
            return do_next_dirent(de, offs);
        }
        return add_dirent(mxtl::move(vndir), de, args, offs->off);
    } else {
        // filled entry, can we sub-divide?
        uint32_t size = static_cast<uint32_t>(DirentSize(de->namelen));
        if (size > reclen) {
            error("bad reclen (smaller than dirent) %u < %u\n", reclen, size);
            return ERR_IO;
        }
        uint32_t extra = reclen - size;
        if (extra < args->reclen) {
            return do_next_dirent(de, offs);
        }
        // shrink existing entry
        bool was_last_record = de->reclen & kMinfsReclenLast;
        de->reclen = size;
        mx_status_t status = vndir->WriteExactInternal(args->txn, de,
                                                       DirentSize(de->namelen),
                                                       offs->off);
        if (status != NO_ERROR) {
            return status;
        }
        offs->off += size;
        // create new entry in the remaining space
        char data[kMinfsMaxDirentSize];
        de = (minfs_dirent_t*) data;
        de->reclen = extra | (was_last_record ? kMinfsReclenLast : 0);
        return add_dirent(mxtl::move(vndir), de, args, offs->off);
    }
}

// Calls a callback 'func' on all direntries in a directory 'vn' with the
// provided arguments, reacting to the return code of the callback.
//
// When 'func' is called, it receives a few arguments:
//  'vndir': The directory on which the callback is operating
//  'de': A pointer the start of a single dirent.
//        Only DirentSize(de->namelen) bytes are guaranteed to exist in
//        memory from this starting pointer.
//  'args': Additional arguments plumbed through ForEachDirent
//  'offs': Offset info about where in the directory this direntry is located.
//          Since 'func' may create / remove surrounding dirents, it is responsible for
//          updating the offset information to access the next dirent.
mx_status_t VnodeMinfs::ForEachDirent(DirArgs* args, const DirentCallback func) {
    char data[kMinfsMaxDirentSize];
    minfs_dirent_t* de = (minfs_dirent_t*) data;
    DirectoryOffset offs = {
        .off = 0,
        .off_prev = 0,
    };
    while (offs.off + MINFS_DIRENT_SIZE < kMinfsMaxDirectorySize) {
        trace(MINFS, "Reading dirent at offset %zd\n", offs.off);
        size_t r;
        mx_status_t status = ReadInternal(data, kMinfsMaxDirentSize, offs.off, &r);
        if (status != NO_ERROR) {
            return status;
        } else if ((status = validate_dirent(de, r, offs.off)) != NO_ERROR) {
            return status;
        }

        switch ((status = func(mxtl::RefPtr<VnodeMinfs>(this), de, args, &offs))) {
        case DIR_CB_NEXT:
            break;
        case DIR_CB_SAVE_SYNC:
            inode_.seq_num++;
            InodeSync(args->txn, kMxFsSyncMtime);
            return NO_ERROR;
        case DIR_CB_DONE:
        default:
            return status;
        }
    }
    return ERR_NOT_FOUND;
}

VnodeMinfs::~VnodeMinfs() {
    if (inode_.link_count == 0) {
#ifdef __Fuchsia__
        if (InitIndirectVmo() == NO_ERROR) {
            fs_->InoFree(vmo_indirect_.get(), inode_, ino_);
        }
#else
        fs_->InoFree(inode_, ino_);
#endif
    }

    fs_->VnodeRelease(this);
#ifdef __Fuchsia__
    if (vmo_.is_valid()) {
        block_fifo_request_t request;
        request.txnid = fs_->bc_->TxnId();
        request.vmoid = vmoid_;
        request.opcode = BLOCKIO_CLOSE_VMO;
        fs_->bc_->Txn(&request, 1);
    }
#endif
}

mx_status_t VnodeMinfs::Open(uint32_t flags) {
    trace(MINFS, "minfs_open() vn=%p(#%u)\n", this, ino_);
    if ((flags & O_DIRECTORY) && !IsDirectory()) {
        return ERR_NOT_DIR;
    }
    return NO_ERROR;
}

ssize_t VnodeMinfs::Read(void* data, size_t len, size_t off) {
    trace(MINFS, "minfs_read() vn=%p(#%u) len=%zd off=%zd\n", this, ino_, len, off);
    if (IsDirectory()) {
        return ERR_NOT_FILE;
    }
    size_t r;
    mx_status_t status = ReadInternal(data, len, off, &r);
    if (status != NO_ERROR) {
        return status;
    }
    return r;
}

// Internal read. Usable on directories.
mx_status_t VnodeMinfs::ReadInternal(void* data, size_t len, size_t off, size_t* actual) {
    // clip to EOF
    if (off >= inode_.size) {
        *actual = 0;
        return NO_ERROR;
    }
    if (len > (inode_.size - off)) {
        len = inode_.size - off;
    }

    mx_status_t status;
#ifdef __Fuchsia__
    if ((status = InitVmo()) != NO_ERROR) {
        return status;
    } else if ((status = vmo_.read(data, off, len, actual)) != NO_ERROR) {
        return status;
    }
#else
    void* start = data;
    uint32_t n = off / kMinfsBlockSize;
    size_t adjust = off % kMinfsBlockSize;

    while ((len > 0) && (n < kMinfsMaxFileBlock)) {
        size_t xfer;
        if (len > (kMinfsBlockSize - adjust)) {
            xfer = kMinfsBlockSize - adjust;
        } else {
            xfer = len;
        }

        uint32_t bno;
        if ((status = GetBno(nullptr, n, &bno)) != NO_ERROR) {
            return status;
        }
        if (bno != 0) {
            char bdata[kMinfsBlockSize];
            if (fs_->bc_->Readblk(bno, bdata)) {
                return ERR_IO;
            }
            memcpy(data, bdata + adjust, xfer);
        } else {
            // If the block is not allocated, just read zeros
            memset(data, 0, xfer);
        }

        adjust = 0;
        len -= xfer;
        data = (void*)((uintptr_t)data + xfer);
        n++;
    }
    *actual = (uintptr_t)data - (uintptr_t)start;
#endif
    return NO_ERROR;
}

ssize_t VnodeMinfs::Write(const void* data, size_t len, size_t off) {
    trace(MINFS, "minfs_write() vn=%p(#%u) len=%zd off=%zd\n", this, ino_, len, off);
    if (IsDirectory()) {
        return ERR_NOT_FILE;
    }
    WriteTxn txn(fs_->bc_.get());
    size_t actual;
    mx_status_t status = WriteInternal(&txn, data, len, off, &actual);
    if (status != NO_ERROR) {
        return status;
    }
    if (actual != 0) {
        InodeSync(&txn, kMxFsSyncMtime);  // Successful writes updates mtime
    }
    return actual;
}

// Internal write. Usable on directories.
mx_status_t VnodeMinfs::WriteInternal(WriteTxn* txn, const void* data,
                                      size_t len, size_t off, size_t* actual) {
    if (len == 0) {
        *actual = 0;
        return NO_ERROR;
    }

    mx_status_t status;
#ifdef __Fuchsia__
    if ((status = InitVmo()) != NO_ERROR) {
        return status;
    }
#endif
    const void* const start = data;
    uint32_t n = static_cast<uint32_t>(off / kMinfsBlockSize);
    size_t adjust = off % kMinfsBlockSize;

    while ((len > 0) && (n < kMinfsMaxFileBlock)) {
        size_t xfer;
        if (len > (kMinfsBlockSize - adjust)) {
            xfer = kMinfsBlockSize - adjust;
        } else {
            xfer = len;
        }

#ifdef __Fuchsia__
        size_t xfer_off = n * kMinfsBlockSize + adjust;
        if ((xfer_off + xfer) > inode_.size) {
            size_t new_size = xfer_off + xfer;
            if ((status = vmo_.set_size(mxtl::roundup(new_size, kMinfsBlockSize))) != NO_ERROR) {
                goto done;
            }
            inode_.size = static_cast<uint32_t>(new_size);
        }

        // Update this block of the in-memory VMO
        if ((status = VmoWriteExact(data, xfer_off, xfer)) != NO_ERROR) {
            return ERR_IO;
        }

        // Update this block on-disk
        uint32_t bno;
        if ((status = GetBno(txn, n, &bno)) != NO_ERROR) {
            return status;
        }
        assert(bno != 0);
        txn->Enqueue(vmoid_, n, bno, 1);
#else
        uint32_t bno;
        if ((status = GetBno(txn, n, &bno)) != NO_ERROR) {
            goto done;
        }
        assert(bno != 0);
        char wdata[kMinfsBlockSize];
        if (fs_->bc_->Readblk(bno, wdata)) {
            return ERR_IO;
        }
        memcpy(wdata + adjust, data, xfer);
        if (fs_->bc_->Writeblk(bno, wdata)) {
            return ERR_IO;
        }
#endif

        adjust = 0;
        len -= xfer;
        data = (void*)((uintptr_t)(data) + xfer);
        n++;
    }

done:
    len = (uintptr_t)data - (uintptr_t)start;
    if (len == 0) {
        // If more than zero bytes were requested, but zero bytes were written,
        // return an error explicitly (rather than zero).
        if (off >= kMinfsMaxFileSize) {
            return ERR_FILE_BIG;
        }

        return ERR_NO_RESOURCES;
    }
    if ((off + len) > inode_.size) {
        inode_.size = static_cast<uint32_t>(off + len);
    }

    *actual = len;
    return NO_ERROR;
}

mx_status_t VnodeMinfs::Lookup(mxtl::RefPtr<fs::Vnode>* out, const char* name, size_t len) {
    trace(MINFS, "minfs_lookup() vn=%p(#%u) name='%.*s'\n", this, ino_, (int)len, name);
    assert(len <= kMinfsMaxNameSize);
    assert(memchr(name, '/', len) == NULL);

    if (!IsDirectory()) {
        error("not directory\n");
        return ERR_NOT_SUPPORTED;
    }

#ifdef NO_DOTDOT
    if (len == 2 && name[0] == '.' && name[1] == '.') {
        // ".." --> "." when every directory is its own root.
        len = 1;
    }
#endif

    return LookupInternal(out, name, len);
}

mx_status_t VnodeMinfs::LookupInternal(mxtl::RefPtr<fs::Vnode>* out,
                                       const char* name, size_t len) {
    DirArgs args = DirArgs();
    args.name = name;
    args.len = len;
    mx_status_t status;
    if ((status = ForEachDirent(&args, cb_dir_find)) < 0) {
        return status;
    }
    mxtl::RefPtr<VnodeMinfs> vn;
    if ((status = fs_->VnodeGet(&vn, args.ino)) < 0) {
        return status;
    }
    *out = mxtl::move(vn);
    return NO_ERROR;
}

mx_status_t VnodeMinfs::Getattr(vnattr_t* a) {
    trace(MINFS, "minfs_getattr() vn=%p(#%u)\n", this, ino_);
    a->mode = DTYPE_TO_VTYPE(MinfsMagicType(inode_.magic));
    a->inode = ino_;
    a->size = inode_.size;
    a->nlink = inode_.link_count;
    a->create_time = inode_.create_time;
    a->modify_time = inode_.modify_time;
    return NO_ERROR;
}

mx_status_t VnodeMinfs::Setattr(vnattr_t* a) {
    int dirty = 0;
    trace(MINFS, "minfs_setattr() vn=%p(#%u)\n", this, ino_);
    if ((a->valid & ~(ATTR_CTIME|ATTR_MTIME)) != 0) {
        return ERR_NOT_SUPPORTED;
    }
    if ((a->valid & ATTR_CTIME) != 0) {
        inode_.create_time = a->create_time;
        dirty = 1;
    }
    if ((a->valid & ATTR_MTIME) != 0) {
        inode_.modify_time = a->modify_time;
        dirty = 1;
    }
    if (dirty) {
        // write to disk, but don't overwrite the time
        WriteTxn txn(fs_->bc_.get());
        InodeSync(&txn, kMxFsSyncDefault);
    }
    return NO_ERROR;
}

typedef struct dircookie {
    size_t off;        // Offset into directory
    uint32_t reserved; // Unused
    uint32_t seqno;    // inode seq no
} dircookie_t;

static_assert(sizeof(dircookie_t) <= sizeof(vdircookie_t),
              "MinFS dircookie too large to fit in IO state");

mx_status_t VnodeMinfs::Readdir(void* cookie, void* dirents, size_t len) {
    trace(MINFS, "minfs_readdir() vn=%p(#%u) cookie=%p len=%zd\n", this, ino_, cookie, len);
    dircookie_t* dc = reinterpret_cast<dircookie_t*>(cookie);
    fs::DirentFiller df(dirents, len);

    if (!IsDirectory()) {
        return ERR_NOT_SUPPORTED;
    }

    size_t off = dc->off;
    size_t r;
    char data[kMinfsMaxDirentSize];
    minfs_dirent_t* de = (minfs_dirent_t*) data;

    if (off != 0 && dc->seqno != inode_.seq_num) {
        // The offset *might* be invalid, if we called Readdir after a directory
        // has been modified. In this case, we need to re-read the directory
        // until we get to the direntry at or after the previously identified offset.

        size_t off_recovered = 0;
        while (off_recovered < off) {
            if (off_recovered + MINFS_DIRENT_SIZE >= kMinfsMaxDirectorySize) {
                goto fail;
            }
            mx_status_t status = ReadInternal(de, kMinfsMaxDirentSize, off_recovered, &r);
            if ((status != NO_ERROR) || (validate_dirent(de, r, off_recovered) != NO_ERROR)) {
                goto fail;
            }
            off_recovered += MinfsReclen(de, off_recovered);
        }
        off = off_recovered;
    }

    while (off + MINFS_DIRENT_SIZE < kMinfsMaxDirectorySize) {
        mx_status_t status = ReadInternal(de, kMinfsMaxDirentSize, off, &r);
        if (status != NO_ERROR) {
            goto fail;
        } else if (validate_dirent(de, r, off) != NO_ERROR) {
            goto fail;
        }

        if (de->ino) {
            mx_status_t status;
            if ((status = df.Next(de->name, de->namelen, de->type)) != NO_ERROR) {
                // no more space
                goto done;
            }
        }

        off += MinfsReclen(de, off);
    }

done:
    // save our place in the dircookie
    dc->off = off;
    dc->seqno = inode_.seq_num;
    r = df.BytesFilled();
    assert(r <= len); // Otherwise, we're overflowing the input buffer.
    return static_cast<mx_status_t>(r);

fail:
    dc->off = 0;
    return ERR_IO;
}

#ifdef __Fuchsia__
VnodeMinfs::VnodeMinfs(Minfs* fs) :
    fs_(fs), vmo_(MX_HANDLE_INVALID), vmo_indirect_(nullptr) {}
#else
VnodeMinfs::VnodeMinfs(Minfs* fs) : fs_(fs) {}
#endif

bool VnodeMinfs::IsRemote() const { return remoter_.IsRemote(); }
mx_handle_t VnodeMinfs::DetachRemote() { return remoter_.DetachRemote(flags_); }
mx_handle_t VnodeMinfs::WaitForRemote() { return remoter_.WaitForRemote(flags_); }
mx_handle_t VnodeMinfs::GetRemote() const { return remoter_.GetRemote(); }
void VnodeMinfs::SetRemote(mx_handle_t remote) { return remoter_.SetRemote(remote); }

mx_status_t VnodeMinfs::Allocate(Minfs* fs, uint32_t type, mxtl::RefPtr<VnodeMinfs>* out) {
    mx_status_t status = AllocateHollow(fs, out);
    if (status != NO_ERROR) {
        return status;
    }
    memset(&(*out)->inode_, 0, sizeof((*out)->inode_));
    (*out)->inode_.magic = MinfsMagic(type);
    (*out)->inode_.create_time = (*out)->inode_.modify_time = minfs_gettime_utc();
    (*out)->inode_.link_count = (type == kMinfsTypeDir ? 2 : 1);
    return NO_ERROR;
}

mx_status_t VnodeMinfs::AllocateHollow(Minfs* fs, mxtl::RefPtr<VnodeMinfs>* out) {
    AllocChecker ac;
    *out = mxtl::AdoptRef(new (&ac) VnodeMinfs(fs));
    if (!ac.check()) {
        return ERR_NO_MEMORY;
    }
    return NO_ERROR;
}

mx_status_t VnodeMinfs::Create(mxtl::RefPtr<fs::Vnode>* out, const char* name, size_t len, uint32_t mode) {
    trace(MINFS, "minfs_create() vn=%p(#%u) name='%.*s' mode=%#x\n",
          this, ino_, (int)len, name, mode);
    assert(len <= kMinfsMaxNameSize);
    assert(memchr(name, '/', len) == NULL);
    if (!IsDirectory()) {
        return ERR_NOT_SUPPORTED;
    }
    if (IsDeletedDirectory()) {
        return ERR_BAD_STATE;
    }

    DirArgs args = DirArgs();
    args.name = name;
    args.len = len;
    // ensure file does not exist
    mx_status_t status;
    if ((status = ForEachDirent(&args, cb_dir_find)) != ERR_NOT_FOUND) {
        return ERR_ALREADY_EXISTS;
    }

    // creating a directory?
    uint32_t type = S_ISDIR(mode) ? kMinfsTypeDir : kMinfsTypeFile;

    WriteTxn txn(fs_->bc_.get());
    // mint a new inode and vnode for it
    mxtl::RefPtr<VnodeMinfs> vn;
    if ((status = fs_->VnodeNew(&txn, &vn, type)) < 0) {
        return status;
    }

    // If the new node is a directory, fill it with '.' and '..'.
    if (type == kMinfsTypeDir) {
        char bdata[DirentSize(1) + DirentSize(2)];
        minfs_dir_init(bdata, vn->ino_, ino_);
        size_t expected = DirentSize(1) + DirentSize(2);
        if (vn->WriteExactInternal(&txn, bdata, expected, 0) != NO_ERROR) {
            return ERR_IO;
        }
        vn->inode_.dirent_count = 2;
        vn->InodeSync(&txn, kMxFsSyncDefault);
    }

    // add directory entry for the new child node
    args.ino = vn->ino_;
    args.type = type;
    args.reclen = static_cast<uint32_t>(DirentSize(static_cast<uint8_t>(len)));
    args.txn = &txn;
    if ((status = ForEachDirent(&args, cb_dir_append)) < 0) {
        return status;
    }

    *out = mxtl::move(vn);
    return NO_ERROR;
}

constexpr const char kFsName[] = "minfs";

ssize_t VnodeMinfs::Ioctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                          size_t out_len) {
    switch (op) {
        case IOCTL_VFS_QUERY_FS: {
            if (out_len < strlen(kFsName) + 1) {
                return ERR_INVALID_ARGS;
            }
            strcpy(static_cast<char*>(out_buf), kFsName);
            return strlen(kFsName);
        }
        case IOCTL_VFS_UNMOUNT_FS: {
            mx_status_t status = Sync();
            if (status != NO_ERROR) {
                error("minfs unmount failed to sync; unmounting anyway: %d\n", status);
            }
            // 'fs_' is deleted after Unmount is called.
            return fs_->Unmount();
        }
        default: {
            return ERR_NOT_SUPPORTED;
        }
    }
}

mx_status_t VnodeMinfs::Unlink(const char* name, size_t len, bool must_be_dir) {
    trace(MINFS, "minfs_unlink() vn=%p(#%u) name='%.*s'\n", this, ino_, (int)len, name);
    assert(len <= kMinfsMaxNameSize);
    assert(memchr(name, '/', len) == NULL);
    if (!IsDirectory()) {
        return ERR_NOT_SUPPORTED;
    }
    if ((len == 1) && (name[0] == '.')) {
        return ERR_BAD_STATE;
    }
    if ((len == 2) && (name[0] == '.') && (name[1] == '.')) {
        return ERR_BAD_STATE;
    }
    WriteTxn txn(fs_->bc_.get());
    DirArgs args = DirArgs();
    args.name = name;
    args.len = len;
    args.type = must_be_dir ? kMinfsTypeDir : 0;
    args.txn = &txn;
    return ForEachDirent(&args, cb_dir_unlink);
}

mx_status_t VnodeMinfs::Truncate(size_t len) {
    if (IsDirectory()) {
        return ERR_NOT_FILE;
    }

    WriteTxn txn(fs_->bc_.get());
    mx_status_t status = TruncateInternal(&txn, len);
    if (status == NO_ERROR) {
        // Successful truncates update inode
        InodeSync(&txn, kMxFsSyncMtime);
    }
    return status;
}

mx_status_t VnodeMinfs::TruncateInternal(WriteTxn* txn, size_t len) {
    mx_status_t r = 0;
#ifdef __Fuchsia__
    // TODO(smklein): We should only init up to 'len'; no need
    // to read in the portion of a large file we plan on deleting.
    if (InitVmo() != NO_ERROR) {
        return ERR_IO;
    }
#endif

    if (len < inode_.size) {
        // Truncate should make the file shorter
        size_t bno = inode_.size / kMinfsBlockSize;
        size_t trunc_bno = len / kMinfsBlockSize;

        // Truncate to the nearest block
        if (trunc_bno <= bno) {
            uint32_t start_bno = static_cast<uint32_t>((len % kMinfsBlockSize == 0) ?
                                                       trunc_bno : trunc_bno + 1);
            if ((r = BlocksShrink(txn, start_bno)) < 0) {
                return r;
            }

            if (start_bno * kMinfsBlockSize < inode_.size) {
                inode_.size = start_bno * kMinfsBlockSize;
            }
        }

        // Write zeroes to the rest of the remaining block, if it exists
        if (len < inode_.size) {
            char bdata[kMinfsBlockSize];
            uint32_t bno;
            if (GetBno(nullptr, static_cast<uint32_t>(len / kMinfsBlockSize), &bno) != NO_ERROR) {
                return ERR_IO;
            }
            if (bno != 0) {
                size_t adjust = len % kMinfsBlockSize;
#ifdef __Fuchsia__
                if ((r = VmoReadExact(bdata, len - adjust, adjust)) != NO_ERROR) {
                    return ERR_IO;
                }
                memset(bdata + adjust, 0, kMinfsBlockSize - adjust);

                // TODO(smklein): Remove this write when shrinking VMO size
                // automatically sets partial pages to zero.
                if ((r = VmoWriteExact(bdata, len - adjust, kMinfsBlockSize)) != NO_ERROR) {
                    return ERR_IO;
                }
#else
                if (fs_->bc_->Readblk(bno, bdata)) {
                    return ERR_IO;
                }
                memset(bdata + adjust, 0, kMinfsBlockSize - adjust);
#endif

                if (fs_->bc_->Writeblk(bno, bdata)) {
                    return ERR_IO;
                }
            }
        }
    } else if (len > inode_.size) {
        // Truncate should make the file longer, filled with zeroes.
        if (kMinfsMaxFileSize < len) {
            return ERR_INVALID_ARGS;
        }
        char zero = 0;
        if ((r = WriteExactInternal(txn, &zero, 1, len - 1)) != NO_ERROR) {
            return r;
        }
    }

    inode_.size = static_cast<uint32_t>(len);
#ifdef __Fuchsia__
    if ((r = vmo_.set_size(mxtl::roundup(len, kMinfsBlockSize))) != NO_ERROR) {
        return r;
    }
#endif

    return NO_ERROR;
}

// verify that the 'newdir' inode is not a subdirectory of the source.
static mx_status_t check_not_subdirectory(mxtl::RefPtr<VnodeMinfs> src, mxtl::RefPtr<VnodeMinfs> newdir) {
    mxtl::RefPtr<VnodeMinfs> vn = newdir;
    mx_status_t status = NO_ERROR;
    while (vn->ino_ != kMinfsRootIno) {
        if (vn->ino_ == src->ino_) {
            status = ERR_INVALID_ARGS;
            break;
        }

        mxtl::RefPtr<fs::Vnode> out = nullptr;
        if ((status = vn->LookupInternal(&out, "..", 2)) < 0) {
            break;
        }
        vn = mxtl::RefPtr<VnodeMinfs>::Downcast(out);
    }
    return status;
}

mx_status_t VnodeMinfs::Rename(mxtl::RefPtr<fs::Vnode> _newdir, const char* oldname, size_t oldlen,
                               const char* newname, size_t newlen, bool src_must_be_dir,
                               bool dst_must_be_dir) {
    auto newdir = mxtl::RefPtr<VnodeMinfs>::Downcast(_newdir);
    trace(MINFS, "minfs_rename() olddir=%p(#%u) newdir=%p(#%u) oldname='%.*s' newname='%.*s'\n",
          this, ino_, newdir.get(), newdir->ino_, (int)oldlen, oldname, (int)newlen, newname);
    assert(oldlen <= kMinfsMaxNameSize);
    assert(memchr(oldname, '/', oldlen) == NULL);
    assert(newlen <= kMinfsMaxNameSize);
    assert(memchr(newname, '/', newlen) == NULL);
    // ensure that the vnodes containing oldname and newname are directories
    if (!(IsDirectory() && newdir->IsDirectory()))
        return ERR_NOT_SUPPORTED;

    // rule out any invalid new/old names
    if ((oldlen == 1) && (oldname[0] == '.'))
        return ERR_BAD_STATE;
    if ((oldlen == 2) && (oldname[0] == '.') && (oldname[1] == '.'))
        return ERR_BAD_STATE;
    if ((newlen == 1) && (newname[0] == '.'))
        return ERR_BAD_STATE;
    if ((newlen == 2) && (newname[0] == '.') && (newname[1] == '.'))
        return ERR_BAD_STATE;

    mx_status_t status;
    mxtl::RefPtr<VnodeMinfs> oldvn = nullptr;
    // acquire the 'oldname' node (it must exist)
    DirArgs args = DirArgs();
    args.name = oldname;
    args.len = oldlen;
    if ((status = ForEachDirent(&args, cb_dir_find)) < 0) {
        return status;
    } else if ((status = fs_->VnodeGet(&oldvn, args.ino)) < 0) {
        return status;
    } else if ((status = check_not_subdirectory(oldvn, newdir)) < 0) {
        return status;
    }

    // If either the 'src' or 'dst' must be directories, BOTH of them must be directories.
    if (!oldvn->IsDirectory() && (src_must_be_dir || dst_must_be_dir)) {
        return ERR_NOT_DIR;
    }

    // if the entry for 'newname' exists, make sure it can be replaced by
    // the vnode behind 'oldname'.
    WriteTxn txn(fs_->bc_.get());
    args.txn = &txn;
    args.name = newname;
    args.len = newlen;
    args.ino = oldvn->ino_;
    args.type = oldvn->IsDirectory() ? kMinfsTypeDir : kMinfsTypeFile;
    status = newdir->ForEachDirent(&args, cb_dir_attempt_rename);
    if (status == ERR_NOT_FOUND) {
        // if 'newname' does not exist, create it
        args.reclen = static_cast<uint32_t>(DirentSize(static_cast<uint8_t>(newlen)));
        if ((status = newdir->ForEachDirent(&args, cb_dir_append)) < 0) {
            return status;
        }
    } else if (status != NO_ERROR) {
        return status;
    }

    // update the oldvn's entry for '..' if (1) it was a directory, and (2) it
    // moved to a new directory
    if ((args.type == kMinfsTypeDir) && (ino_ != newdir->ino_)) {
        mxtl::RefPtr<fs::Vnode> vn_fs;
        if ((status = newdir->Lookup(&vn_fs, newname, newlen)) < 0) {
            return status;
        }
        auto vn = mxtl::RefPtr<VnodeMinfs>::Downcast(vn_fs);
        args.name = "..";
        args.len = 2;
        args.ino = newdir->ino_;
        if ((status = vn->ForEachDirent(&args, cb_dir_update_inode)) < 0) {
            return status;
        }
    }

    // at this point, the oldvn exists with multiple names (or the same name in
    // different directories)
    oldvn->inode_.link_count++;

    // finally, remove oldname from its original position
    args.name = oldname;
    args.len = oldlen;
    return ForEachDirent(&args, cb_dir_force_unlink);
}

mx_status_t VnodeMinfs::Link(const char* name, size_t len, mxtl::RefPtr<fs::Vnode> _target) {
    trace(MINFS, "minfs_link() vndir=%p(#%u) name='%.*s'\n", this, ino_, (int)len, name);
    assert(len <= kMinfsMaxNameSize);
    assert(memchr(name, '/', len) == NULL);
    if (!IsDirectory()) {
        return ERR_NOT_SUPPORTED;
    } else if (IsDeletedDirectory()) {
        return ERR_BAD_STATE;
    }

    // rule out any invalid names
    if ((len == 1) && (name[0] == '.'))
        return ERR_BAD_STATE;
    if ((len == 2) && (name[0] == '.') && (name[1] == '.'))
        return ERR_BAD_STATE;

    auto target = mxtl::RefPtr<VnodeMinfs>::Downcast(_target);
    if (target->IsDirectory()) {
        // The target must not be a directory
        return ERR_NOT_FILE;
    }

    // The destination should not exist
    DirArgs args = DirArgs();
    args.name = name;
    args.len = len;
    mx_status_t status;
    if ((status = ForEachDirent(&args, cb_dir_find)) != ERR_NOT_FOUND) {
        return (status == NO_ERROR) ? ERR_ALREADY_EXISTS : status;
    }

    WriteTxn txn(fs_->bc_.get());
    args.ino = target->ino_;
    args.type = kMinfsTypeFile; // We can't hard link directories
    args.reclen = static_cast<uint32_t>(DirentSize(static_cast<uint8_t>(len)));
    args.txn = &txn;
    if ((status = ForEachDirent(&args, cb_dir_append)) < 0) {
        return status;
    }

    // We have successfully added the vn to a new location. Increment the link count.
    target->inode_.link_count++;
    target->InodeSync(&txn, kMxFsSyncDefault);

    return NO_ERROR;
}

mx_status_t VnodeMinfs::Sync() {
    return fs_->bc_->Sync();
}

mx_status_t VnodeMinfs::AttachRemote(mx_handle_t h) {
    if (!IsDirectory() || IsDeletedDirectory()) {
        return ERR_NOT_DIR;
    } else if (IsRemote()) {
        return ERR_ALREADY_BOUND;
    }
    SetRemote(h);
    return NO_ERROR;
}

} // namespace minfs
