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
#include <magenta/device/devmgr.h>

#ifdef __Fuchsia__
#include <magenta/syscalls.h>
#include <mxio/vfs.h>
#endif

#include "minfs-private.h"

namespace {

mx_time_t minfs_gettime_utc() {
    // linux/magenta compatible
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    mx_time_t time = MX_SEC(ts.tv_sec)+ts.tv_nsec;
    return time;
}

#ifdef __Fuchsia__
mx_status_t vmo_read_exact(mx_handle_t h, void* data, uint64_t offset, size_t len) {
    size_t actual;
    mx_status_t status = mx_vmo_read(h, data, offset, len, &actual);
    if (status != NO_ERROR) {
        return status;
    } else if (actual != len) {
        return ERR_IO;
    }
    return NO_ERROR;
}

mx_status_t vmo_write_exact(mx_handle_t h, const void* data, uint64_t offset, size_t len) {
    size_t actual;
    mx_status_t status = mx_vmo_write(h, data, offset, len, &actual);
    if (status != NO_ERROR) {
        return status;
    } else if (actual != len) {
        return ERR_IO;
    }
    return NO_ERROR;
}
#endif


} // namespace anonymous


namespace minfs {

//TODO: better bitmap block read/write functions

// TODO(smklein): Once vmo support is complete, we should write to bitmaps via VMOs, and naturally
// delay flushing their dirty blocks to the disk, rather than using these helpers.

// helper for updating many bitmap entries
// if the next entry is in the same block, defer
// write until a different block is needed
mxtl::RefPtr<BlockNode> Minfs::BitmapBlockGet(const mxtl::RefPtr<BlockNode>& blk,
                                              uint32_t n) {
    uint32_t bitblock = n / kMinfsBlockBits; // Relative to bitmap
    if (blk) {
        uint32_t bitblock_old = blk->GetKey() - info_.abm_block;
        if (bitblock_old == bitblock) {
            // same block as before, nothing to do
            return blk;
        }
        // write previous block to disk
        const void* src = GetBlock(block_map_, bitblock_old);
        memcpy(blk->data(), src, kMinfsBlockSize);
        bc_->Put(blk, kBlockDirty);
    }
    return mxtl::RefPtr<BlockNode>(bc_->Get(info_.abm_block + bitblock));
}

void Minfs::BitmapBlockPut(const mxtl::RefPtr<BlockNode>& blk) {
    if (blk) {
        uint32_t bitblock = blk->GetKey() - info_.abm_block;
        const void* src = GetBlock(block_map_, bitblock);
        memcpy(blk->data(), src, kMinfsBlockSize);
        bc_->Put(blk, kBlockDirty);
    }
}

mx_status_t VnodeMinfs::InodeDestroy() {
    minfs_inode_t inode;

    trace(MINFS, "InodeDestroy() ino=%u\n", ino_);

    // save local copy, destroy inode on disk
    memcpy(&inode, &inode_, sizeof(inode));
    memset(&inode_, 0, sizeof(inode));
    InodeSync(kMxFsSyncDefault);
    return fs_->InoFree(inode, ino_);
}

void VnodeMinfs::InodeSync(uint32_t flags) {
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

    fs_->InodeSync(ino_, &inode_);
}

// Delete all blocks (relative to a file) from "start" (inclusive) to the end of
// the file. Does not update mtime/atime.
mx_status_t VnodeMinfs::BlocksShrink(uint32_t start) {
    mxtl::RefPtr<BlockNode> bitmap_blk = nullptr;

    bool doSync = false;

    // release direct blocks
    for (unsigned bno = start; bno < kMinfsDirect; bno++) {
        if (inode_.dnum[bno] == 0) {
            continue;
        }
        if ((bitmap_blk = fs_->BitmapBlockGet(bitmap_blk, inode_.dnum[bno])) == nullptr) {
            return ERR_IO;
        }

        fs_->block_map_.Clear(inode_.dnum[bno], inode_.dnum[bno] + 1);
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
        unsigned bno = kMinfsDirect + (indirect + 1) * direct_per_indirect;
        if (start > bno) {
            continue;
        }
        mxtl::RefPtr<BlockNode> blk = nullptr;
        if ((blk = fs_->bc_->Get(inode_.inum[indirect])) == nullptr) {
            fs_->BitmapBlockPut(bitmap_blk);
            return ERR_IO;
        }
        uint32_t* entry = static_cast<uint32_t*>(blk->data());
        uint32_t iflags = 0;
        bool delete_indirect = true; // can we delete the indirect block?
        // release the blocks pointed at by the entries in the indirect block
        for (unsigned direct = 0; direct < direct_per_indirect; direct++) {
            if (entry[direct] == 0) {
                continue;
            }
            unsigned bno = kMinfsDirect + indirect * direct_per_indirect + direct;
            if (start > bno) {
                // This is a valid entry which exists in the indirect block
                // BEFORE our truncation point. Don't delete it, and don't
                // delete the indirect block.
                delete_indirect = false;
                continue;
            }

            if ((bitmap_blk = fs_->BitmapBlockGet(bitmap_blk, entry[direct])) == nullptr) {
                fs_->bc_->Put(blk, iflags);
                return ERR_IO;
            }
            fs_->block_map_.Clear(entry[direct], entry[direct] + 1);
            entry[direct] = 0;
            iflags = kBlockDirty;
            inode_.block_count--;
        }
        // only update the indirect block if an entry was deleted
        if (iflags & kBlockDirty) {
            doSync = true;
        }
        fs_->bc_->Put(blk, iflags);

        if (delete_indirect)  {
            // release the direct block itself
            bitmap_blk = fs_->BitmapBlockGet(bitmap_blk, inode_.inum[indirect]);
            if (bitmap_blk == nullptr) {
                return ERR_IO;
            }
            fs_->block_map_.Clear(inode_.inum[indirect], inode_.inum[indirect] + 1);
            inode_.inum[indirect] = 0;
            inode_.block_count--;
            doSync = true;
        }
    }

    if (doSync) {
        InodeSync(kMxFsSyncDefault);
    }
    fs_->BitmapBlockPut(bitmap_blk);
    return NO_ERROR;
}

#ifdef __Fuchsia__
// Read data from disk at block 'bno', into the 'nth' logical block of the file.
mx_status_t VnodeMinfs::FillBlock(uint32_t n, uint32_t bno) {
    // TODO(smklein): read directly from block device into vmo; no need to copy
    // into an intermediate buffer.
    char bdata[kMinfsBlockSize];
    if (fs_->bc_->Readblk(bno, bdata)) {
        return ERR_IO;
    }
    mx_status_t status = vmo_write_exact(vmo_, bdata, n * kMinfsBlockSize, kMinfsBlockSize);
    if (status != NO_ERROR) {
        return status;
    }
    return NO_ERROR;
}

// Since we cannot yet register the filesystem as a paging service (and cleanly
// fault on pages when they are actually needed), we currently read an entire
// file to a VMO when a file's data block are accessed.
//
// TODO(smklein): Even this hack can be optimized; a bitmap could be used to
// track all 'empty/read/dirty' blocks for each vnode, rather than reading
// the entire file.
mx_status_t VnodeMinfs::InitVmo() {
    if (vmo_ != MX_HANDLE_INVALID) {
        return NO_ERROR;
    }

    mx_status_t status;
    if ((status = mx_vmo_create(mxtl::roundup(inode_.size, kMinfsBlockSize), 0, &vmo_)) != NO_ERROR) {
        error("Failed to initialize vmo; error: %d\n", status);
        return status;
    }

    // Initialize all direct blocks
    uint32_t bno;
    for (uint32_t d = 0; d < kMinfsDirect; d++) {
        if ((bno = inode_.dnum[d]) != 0) {
            if ((status = FillBlock(d, bno)) != NO_ERROR) {
                error("Failed to fill bno %u; error: %d\n", bno, status);
                return status;
            }
        }
    }

    // Initialize all indirect blocks
    for (uint32_t i = 0; i < kMinfsIndirect; i++) {
        uint32_t ibno;
        mxtl::RefPtr<BlockNode> iblk;
        if ((ibno = inode_.inum[i]) != 0) {
            // TODO(smklein): Should there be a separate vmo for indirect blocks?
            if ((iblk = fs_->bc_->Get(ibno)) == nullptr) {
                return ERR_IO;
            }
            uint32_t* ientry = static_cast<uint32_t*>(iblk->data());

            const uint32_t direct_per_indirect = kMinfsBlockSize / sizeof(uint32_t);
            for (uint32_t j = 0; j < direct_per_indirect; j++) {
                if ((bno = ientry[j]) != 0) {
                    uint32_t n = kMinfsDirect + i * direct_per_indirect + j;
                    if ((status = FillBlock(n, bno)) != NO_ERROR) {
                        fs_->bc_->Put(iblk, 0);
                        return status;
                    }
                }
            }
            fs_->bc_->Put(iblk, 0);
        }
    }

    return NO_ERROR;
}
#endif

// Get the bno corresponding to the nth logical block within the file.
mx_status_t VnodeMinfs::GetBno(uint32_t n, uint32_t* bno, bool alloc) {
    uint32_t hint = 0;
    // direct blocks are simple... is there an entry in dnum[]?
    if (n < kMinfsDirect) {
        if (((*bno = inode_.dnum[n]) == 0) && alloc) {
            mx_status_t status = fs_->BlockNew(hint, bno, nullptr);
            if (status != NO_ERROR) {
                return status;
            }
            inode_.dnum[n] = *bno;
            inode_.block_count++;
            InodeSync(kMxFsSyncDefault);
        }
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

    uint32_t ibno;
    mxtl::RefPtr<BlockNode> iblk;
    uint32_t iflags = 0;

    // look up the indirect bno
    if ((ibno = inode_.inum[i]) == 0) {
        if (!alloc) {
            *bno = 0;
            return NO_ERROR;
        }
        // allocate a new indirect block
        mx_status_t status = fs_->BlockNew(0, &ibno, &iblk);
        if (status != NO_ERROR) {
            return status;
        }
        // record new indirect block in inode, note that we need to update
        inode_.block_count++;
        inode_.inum[i] = ibno;
        iflags = kBlockDirty;
    } else {
        if ((iblk = fs_->bc_->Get(ibno)) == nullptr) {
            return ERR_IO;
        }
    }
    uint32_t* ientry = static_cast<uint32_t*>(iblk->data());

    if (((*bno = ientry[j]) == 0) && alloc) {
        // allocate a new block
        mx_status_t status = fs_->BlockNew(hint, bno, nullptr);
        if (status != NO_ERROR) {
            fs_->bc_->Put(iblk, iflags);
            return status;
        }
        inode_.block_count++;
        ientry[j] = *bno;
        iflags = kBlockDirty;
    }

    // release indirect block, updating if necessary
    // and update the inode as well if we changed it
    fs_->bc_->Put(iblk, iflags);
    if (iflags & kBlockDirty) {
        InodeSync(kMxFsSyncDefault);
    }

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

mx_status_t VnodeMinfs::WriteExactInternal(const void* data, size_t len, size_t off) {
    size_t actual;
    mx_status_t status = WriteInternal(data, len, off, &actual);
    if (status != NO_ERROR) {
        return status;
    } else if (actual != len) {
        return ERR_IO;
    }
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

static mx_status_t cb_dir_find(VnodeMinfs* vndir, minfs_dirent_t* de, DirArgs* args,
                               DirectoryOffset* offs) {
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

// UnlinkChild is always called with a vn which has been acquired at least once.
// Before returning, 'vn' MUST be released.
mx_status_t VnodeMinfs::UnlinkChild(VnodeMinfs* childvn, minfs_dirent_t* de, DirectoryOffset* offs) {
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
            goto fail;
        } else if ((status = validate_dirent(&de_next, len, off_next)) != NO_ERROR) {
            error("unlink: Read invalid dirent\n");
            goto fail;
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
            goto fail;
        } else if ((status = validate_dirent(&de_prev, len, off_prev)) != NO_ERROR) {
            error("unlink: Read invalid dirent\n");
            goto fail;
        }
        if (de_prev.ino == 0) {
            coalesced_size += MinfsReclen(&de_prev, off_prev);
            off = off_prev;
        }
    }

    if (!(de->reclen & kMinfsReclenLast) && (coalesced_size >= kMinfsReclenMask)) {
        // Should only be possible if the on-disk record format is corrupted
        status = ERR_IO;
        goto fail;
    }
    de->ino = 0;
    de->reclen = static_cast<uint32_t>(coalesced_size & kMinfsReclenMask) |
        (de->reclen & kMinfsReclenLast);
    // Erase dirent (replace with 'empty' dirent)
    if ((status = WriteExactInternal(de, MINFS_DIRENT_SIZE, off)) != NO_ERROR) {
        goto fail;
    }

    if (de->reclen & kMinfsReclenLast) {
        // Truncating the directory merely removed unused space; if it fails,
        // the directory contents are still valid.
        TruncateInternal(off + MINFS_DIRENT_SIZE);
    }

    inode_.dirent_count--;
    // This effectively 'unlinks' the target node without deleting the direntry
    childvn->inode_.link_count--;

    if (MinfsMagicType(childvn->inode_.magic) == kMinfsTypeDir) {
        // Child directory had '..' which pointed to parent directory
        inode_.link_count--;
        if (childvn->inode_.link_count == 1) {
            // Directories are initialized with two links, since they point
            // to themselves via ".". Thus, when they reach "one link", they
            // are only pointed to by themselves, and should be deleted.
            childvn->inode_.link_count--;
            childvn->flags_ |= kMinfsFlagDeletedDirectory;
        }
    }

    childvn->InodeSync(kMxFsSyncMtime);
    childvn->RefRelease();
    return DIR_CB_SAVE_SYNC;

fail:
    childvn->RefRelease();
    return status;
}

// caller is expected to prevent unlink of "." or ".."
static mx_status_t cb_dir_unlink(VnodeMinfs* vndir, minfs_dirent_t* de,
                                 DirArgs* args, DirectoryOffset* offs) {
    if ((de->ino == 0) || (args->len != de->namelen) ||
        memcmp(args->name, de->name, args->len)) {
        return do_next_dirent(de, offs);
    }

    VnodeMinfs* vn;
    mx_status_t status;
    if ((status = vndir->fs_->VnodeGet(&vn, de->ino)) < 0) { // vn refcount +1
        return status;
    }

    // If a directory was requested, then only try unlinking a directory
    if ((args->type == kMinfsTypeDir) && !vn->IsDirectory()) {
        vn->RefRelease(); // vn refcount +0
        return ERR_NOT_DIR;
    }
    if (!vn->CanUnlink()) {
        vn->RefRelease(); // vn refcount +0
        return ERR_BAD_STATE;
    }
    return vndir->UnlinkChild(vn, de, offs); // vn refcount +1
}

// same as unlink, but do not validate vnode
static mx_status_t cb_dir_force_unlink(VnodeMinfs* vndir, minfs_dirent_t* de,
                                       DirArgs* args, DirectoryOffset* offs) {
    if ((de->ino == 0) || (args->len != de->namelen) ||
        memcmp(args->name, de->name, args->len)) {
        return do_next_dirent(de, offs);
    }

    VnodeMinfs* vn;
    mx_status_t status;
    if ((status = vndir->fs_->VnodeGet(&vn, de->ino)) < 0) { // vn refcount +1
        return status;
    }
    return vndir->UnlinkChild(vn, de, offs); // vn refcount +1
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
static mx_status_t cb_dir_attempt_rename(VnodeMinfs* vndir, minfs_dirent_t* de,
                                         DirArgs* args, DirectoryOffset* offs) {
    if ((de->ino == 0) || (args->len != de->namelen) ||
        memcmp(args->name, de->name, args->len)) {
        return do_next_dirent(de, offs);
    }

    VnodeMinfs* vn;
    mx_status_t status;
    if ((status = vndir->fs_->VnodeGet(&vn, de->ino)) < 0) {
        return status;
    } else if (args->ino == vn->ino_) {
        // cannot rename node to itself
        vn->RefRelease();
        return ERR_BAD_STATE;
    } else if (args->type != de->type) {
        // cannot rename directory to file (or vice versa)
        vn->RefRelease();
        return ERR_BAD_STATE;
    } else if (!vn->CanUnlink()) {
        // if we cannot unlink the target, we cannot rename the target
        vn->RefRelease();
        return ERR_BAD_STATE;
    }

    vn->inode_.link_count--;
    vn->RefRelease();

    de->ino = args->ino;
    status = vndir->WriteExactInternal(de, DirentSize(de->namelen), offs->off);
    if (status != NO_ERROR) {
        return status;
    }
    return DIR_CB_SAVE_SYNC;
}

static mx_status_t cb_dir_update_inode(VnodeMinfs* vndir, minfs_dirent_t* de,
                                       DirArgs* args, DirectoryOffset* offs) {
    if ((de->ino == 0) || (args->len != de->namelen) ||
        memcmp(args->name, de->name, args->len)) {
        return do_next_dirent(de, offs);
    }

    de->ino = args->ino;
    mx_status_t status = vndir->WriteExactInternal(de, DirentSize(de->namelen), offs->off);
    if (status != NO_ERROR) {
        return status;
    }
    return DIR_CB_SAVE_SYNC;
}

static mx_status_t add_dirent(VnodeMinfs* vndir, minfs_dirent_t* de, DirArgs* args, size_t off) {
    de->ino = args->ino;
    de->type = static_cast<uint8_t>(args->type);
    de->namelen = static_cast<uint8_t>(args->len);
    memcpy(de->name, args->name, args->len);
    mx_status_t status = vndir->WriteExactInternal(de, DirentSize(de->namelen), off);
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

static mx_status_t cb_dir_append(VnodeMinfs* vndir, minfs_dirent_t* de,
                                 DirArgs* args, DirectoryOffset* offs) {
    uint32_t reclen = static_cast<uint32_t>(MinfsReclen(de, offs->off));
    if (de->ino == 0) {
        // empty entry, do we fit?
        if (args->reclen > reclen) {
            return do_next_dirent(de, offs);
        }
        return add_dirent(vndir, de, args, offs->off);
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
        mx_status_t status = vndir->WriteExactInternal(de, DirentSize(de->namelen), offs->off);
        if (status != NO_ERROR) {
            return status;
        }
        offs->off += size;
        // create new entry in the remaining space
        de = (minfs_dirent_t*) ((uintptr_t)de + size);
        char data[kMinfsMaxDirentSize];
        de = (minfs_dirent_t*) data;
        de->reclen = extra | (was_last_record ? kMinfsReclenLast : 0);
        return add_dirent(vndir, de, args, offs->off);
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
mx_status_t VnodeMinfs::ForEachDirent(DirArgs* args,
                                      mx_status_t (*func)(VnodeMinfs*, minfs_dirent_t*, DirArgs*,
                                                          DirectoryOffset*)) {
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

        switch ((status = func(this, de, args, &offs))) {
        case DIR_CB_NEXT:
            break;
        case DIR_CB_SAVE_SYNC:
            inode_.seq_num++;
            InodeSync(kMxFsSyncMtime);
            return NO_ERROR;
        case DIR_CB_DONE:
        default:
            return status;
        }
    }
    return ERR_NOT_FOUND;
}

void VnodeMinfs::Release() {
    trace(MINFS, "minfs_release() vn=%p(#%u)%s\n", this, ino_,
          inode_.link_count ? "" : " link-count is zero");
    if (inode_.link_count == 0) {
        InodeDestroy();
    }

    fs_->VnodeRelease(this);
#ifdef __Fuchsia__
    mx_handle_close(vmo_);
#endif
    delete this;
}

mx_status_t VnodeMinfs::Open(uint32_t flags) {
    trace(MINFS, "minfs_open() vn=%p(#%u)\n", this, ino_);
    if ((flags & O_DIRECTORY) && !IsDirectory()) {
        return ERR_NOT_DIR;
    }
    RefAcquire();
    return NO_ERROR;
}

mx_status_t VnodeMinfs::Close() {
    trace(MINFS, "minfs_close() vn=%p(#%u)\n", this, ino_);
    RefRelease();
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
    } else if ((status = mx_vmo_read(vmo_, data, off, len, actual)) != NO_ERROR) {
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
        if ((status = GetBno(n, &bno, false)) != NO_ERROR) {
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
    size_t actual;
    mx_status_t status = WriteInternal(data, len, off, &actual);
    if (status != NO_ERROR) {
        return status;
    }
    if (actual != 0) {
        InodeSync(kMxFsSyncMtime);  // Successful writes updates mtime
    }
    return actual;
}

// Internal write. Usable on directories.
mx_status_t VnodeMinfs::WriteInternal(const void* data, size_t len, size_t off, size_t* actual) {
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
            if ((status = mx_vmo_set_size(vmo_, mxtl::roundup(new_size, kMinfsBlockSize))) != NO_ERROR) {
                goto done;
            }
            inode_.size = static_cast<uint32_t>(new_size);
        }

        // TODO(smklein): If a failure occurs after writing to the VMO, but
        // before updating the data to disk, then our in-memory representation
        // of the file may not be consistent with the on-disk representation of
        // the file. As a consequence, an error is returned (ERR_IO) rather than
        // doing a partial read.

        // Update this block of the in-memory VMO
        if ((status = vmo_write_exact(vmo_, data, xfer_off, xfer)) != NO_ERROR) {
            return ERR_IO;
        }

        // Update this block on-disk
        char bdata[kMinfsBlockSize];
        // TODO(smklein): Can we write directly from the VMO to the block device,
        // preventing the need for a 'bdata' variable?
        if (xfer != kMinfsBlockSize) {
            if (vmo_read_exact(vmo_, bdata, n * kMinfsBlockSize, kMinfsBlockSize) != NO_ERROR) {
                return ERR_IO;
            }
        }
        const void* wdata = (xfer != kMinfsBlockSize) ? bdata : data;
        uint32_t bno;
        if ((status = GetBno(n, &bno, true)) != NO_ERROR) {
            return status;
        }
        assert(bno != 0);
        if (fs_->bc_->Writeblk(bno, wdata)) {
            return ERR_IO;
        }
#else
        uint32_t bno;
        if ((status = GetBno(n, &bno, true)) != NO_ERROR) {
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

mx_status_t VnodeMinfs::Lookup(fs::Vnode** out, const char* name, size_t len) {
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

mx_status_t VnodeMinfs::LookupInternal(fs::Vnode** out, const char* name, size_t len) {
    DirArgs args = DirArgs();
    args.name = name;
    args.len = len;
    mx_status_t status;
    if ((status = ForEachDirent(&args, cb_dir_find)) < 0) {
        return status;
    }
    VnodeMinfs* vn;
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
        InodeSync(kMxFsSyncDefault);
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
    vdirent_t* out = reinterpret_cast<vdirent_t*>(dirents);

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
            size_t len_remaining = len - (size_t)((uintptr_t)out - (uintptr_t)dirents);
            if ((status = fs::vfs_fill_dirent(out, len_remaining, de->name,
                                              de->namelen, de->type)) < 0) {
                // no more space
                goto done;
            }
            out = (vdirent_t*)((uintptr_t)out + status);
        }

        off += MinfsReclen(de, off);
    }

done:
    // save our place in the dircookie
    dc->off = off;
    dc->seqno = inode_.seq_num;
    r = static_cast<size_t>(((uintptr_t) out - (uintptr_t)dirents));
    assert(r <= len); // Otherwise, we're overflowing the input buffer.
    return static_cast<mx_status_t>(r);

fail:
    dc->off = 0;
    return ERR_IO;
}

#ifdef __Fuchsia__
VnodeMinfs::VnodeMinfs(Minfs* fs) : fs_(fs), vmo_(MX_HANDLE_INVALID) {}
#else
VnodeMinfs::VnodeMinfs(Minfs* fs) : fs_(fs) {}
#endif

bool VnodeMinfs::IsRemote() const { return remoter_.IsRemote(); }
mx_handle_t VnodeMinfs::DetachRemote() { return remoter_.DetachRemote(flags_); }
mx_handle_t VnodeMinfs::WaitForRemote() { return remoter_.WaitForRemote(flags_); }
mx_handle_t VnodeMinfs::GetRemote() const { return remoter_.GetRemote(); }
void VnodeMinfs::SetRemote(mx_handle_t remote) { return remoter_.SetRemote(remote); }

mx_status_t VnodeMinfs::Allocate(Minfs* fs, uint32_t type, VnodeMinfs** out) {
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

mx_status_t VnodeMinfs::AllocateHollow(Minfs* fs, VnodeMinfs** out) {
    AllocChecker ac;
    *out = new (&ac) VnodeMinfs(fs);
    if (!ac.check()) {
        return ERR_NO_MEMORY;
    }
    return NO_ERROR;
}

mx_status_t VnodeMinfs::Create(fs::Vnode** out, const char* name, size_t len, uint32_t mode) {
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

    // mint a new inode and vnode for it
    VnodeMinfs* vn;
    if ((status = fs_->VnodeNew(&vn, type)) < 0) { // vn refcount +1
        return status;
    }

    // If the new node is a directory, fill it with '.' and '..'.
    if (type == kMinfsTypeDir) {
        char bdata[DirentSize(1) + DirentSize(2)];
        minfs_dir_init(bdata, vn->ino_, ino_);
        size_t expected = DirentSize(1) + DirentSize(2);
        if (vn->WriteExactInternal(bdata, expected, 0) != NO_ERROR) {
            vn->Release(); // vn refcount +0
            return ERR_IO;
        }
        vn->inode_.dirent_count = 2;
        vn->InodeSync(kMxFsSyncDefault);
    }

    // add directory entry for the new child node
    args.ino = vn->ino_;
    args.type = type;
    args.reclen = static_cast<uint32_t>(DirentSize(static_cast<uint8_t>(len)));
    if ((status = ForEachDirent(&args, cb_dir_append)) < 0) {
        vn->Release(); // vn refcount +0
        return status;
    }

    *out = vn; // vn refcount returned as +1
    return NO_ERROR;
}

constexpr const char kFsName[] = "minfs";

ssize_t VnodeMinfs::Ioctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                          size_t out_len) {
    switch (op) {
        case IOCTL_DEVMGR_QUERY_FS: {
            if (out_len < strlen(kFsName) + 1) {
                return ERR_INVALID_ARGS;
            }
            strcpy(static_cast<char*>(out_buf), kFsName);
            return strlen(kFsName);
        }
        case IOCTL_DEVMGR_UNMOUNT_FS: {
            mx_status_t status = Sync();
            if (status != NO_ERROR) {
                error("minfs unmount failed to sync; unmounting anyway: %d\n", status);
            }
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
    DirArgs args = DirArgs();
    args.name = name;
    args.len = len;
    args.type = must_be_dir ? kMinfsTypeDir : 0;
    return ForEachDirent(&args, cb_dir_unlink);
}

mx_status_t VnodeMinfs::Truncate(size_t len) {
    if (IsDirectory()) {
        return ERR_NOT_FILE;
    }

    mx_status_t status = TruncateInternal(len);
    if (status != NO_ERROR) {
        // Successful truncates update inode
        InodeSync(kMxFsSyncMtime);
    }
    return status;
}

mx_status_t VnodeMinfs::TruncateInternal(size_t len) {
    mx_status_t r = 0;
#ifdef __Fuchsia__
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
            if ((r = BlocksShrink(start_bno)) < 0) {
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
            if (GetBno(static_cast<uint32_t>(len / kMinfsBlockSize), &bno, false) != NO_ERROR) {
                return ERR_IO;
            }
            if (bno != 0) {
                size_t adjust = len % kMinfsBlockSize;
#ifdef __Fuchsia__
                if ((r = vmo_read_exact(vmo_, bdata, len - adjust, adjust)) != NO_ERROR) {
                    return ERR_IO;
                }
                memset(bdata + adjust, 0, kMinfsBlockSize - adjust);

                // TODO(smklein): Remove this write when shrinking VMO size
                // automatically sets partial pages to zero.
                if ((r = vmo_write_exact(vmo_, bdata, len - adjust, kMinfsBlockSize)) != NO_ERROR) {
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
        inode_.size = static_cast<uint32_t>(len);
    } else if (len > inode_.size) {
        // Truncate should make the file longer, filled with zeroes.
        if (kMinfsMaxFileSize < len) {
            return ERR_INVALID_ARGS;
        }
        char zero = 0;
        if ((r = WriteExactInternal(&zero, 1, len - 1)) != NO_ERROR) {
            return r;
        }
    }

#ifdef __Fuchsia__
    if ((r = mx_vmo_set_size(vmo_, mxtl::roundup(len, kMinfsBlockSize))) != NO_ERROR) {
        return r;
    }
#endif

    return NO_ERROR;
}

// verify that the 'newdir' inode is not a subdirectory of the source.
static mx_status_t check_not_subdirectory(VnodeMinfs* src, VnodeMinfs* newdir) {
    VnodeMinfs* vn = newdir;
    mx_status_t status = NO_ERROR;
    // Acquire vn here so this function remains cleanly idempotent with respect
    // to refcounts. 'newdir' and all ancestors (until an exit condition is
    // reached) will be acquired once and released once.
    vn->RefAcquire();
    while (vn->ino_ != kMinfsRootIno) {
        if (vn->ino_ == src->ino_) {
            status = ERR_INVALID_ARGS;
            break;
        }

        fs::Vnode* out = nullptr;
        if ((status = vn->LookupInternal(&out, "..", 2)) < 0) {
            break;
        }
        vn->RefRelease();
        vn = static_cast<VnodeMinfs*>(out);
    }
    vn->RefRelease();
    return status;
}

mx_status_t VnodeMinfs::Rename(fs::Vnode* _newdir, const char* oldname, size_t oldlen,
                               const char* newname, size_t newlen, bool src_must_be_dir,
                               bool dst_must_be_dir) {
    VnodeMinfs* newdir = static_cast<VnodeMinfs*>(_newdir);
    trace(MINFS, "minfs_rename() olddir=%p(#%u) newdir=%p(#%u) oldname='%.*s' newname='%.*s'\n",
          this, ino_, newdir, newdir->ino_, (int)oldlen, oldname, (int)newlen, newname);
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
    VnodeMinfs* oldvn = nullptr;
    // acquire the 'oldname' node (it must exist)
    DirArgs args = DirArgs();
    args.name = oldname;
    args.len = oldlen;
    if ((status = ForEachDirent(&args, cb_dir_find)) < 0) {
        return status;
    } else if ((status = fs_->VnodeGet(&oldvn, args.ino)) < 0) {
        return status;
    } else if ((status = check_not_subdirectory(oldvn, newdir)) < 0) {
        goto done;
    }

    // If either the 'src' or 'dst' must be directories, BOTH of them must be directories.
    if (!oldvn->IsDirectory() && (src_must_be_dir || dst_must_be_dir)) {
        status = ERR_NOT_DIR;
        goto done;
    }

    // if the entry for 'newname' exists, make sure it can be replaced by
    // the vnode behind 'oldname'.
    args.name = newname;
    args.len = newlen;
    args.ino = oldvn->ino_;
    args.type = oldvn->IsDirectory() ? kMinfsTypeDir : kMinfsTypeFile;
    status = newdir->ForEachDirent(&args, cb_dir_attempt_rename);
    if (status == ERR_NOT_FOUND) {
        // if 'newname' does not exist, create it
        args.reclen = static_cast<uint32_t>(DirentSize(static_cast<uint8_t>(newlen)));
        if ((status = newdir->ForEachDirent(&args, cb_dir_append)) < 0) {
            goto done;
        }
        status = NO_ERROR;
    } else if (status != NO_ERROR) {
        goto done;
    }

    // update the oldvn's entry for '..' if (1) it was a directory, and (2) it
    // moved to a new directory
    if ((args.type == kMinfsTypeDir) && (ino_ != newdir->ino_)) {
        fs::Vnode* vn_fs;
        if ((status = newdir->Lookup(&vn_fs, newname, newlen)) < 0) {
            goto done;
        }
        VnodeMinfs* vn = static_cast<VnodeMinfs*>(vn_fs);
        args.name = "..";
        args.len = 2;
        args.ino = newdir->ino_;
        if ((status = vn->ForEachDirent(&args, cb_dir_update_inode)) < 0) {
            vn->RefRelease();
            goto done;
        }
        vn->RefRelease();
    }

    // at this point, the oldvn exists with multiple names (or the same name in
    // different directories)
    oldvn->inode_.link_count++;

    // finally, remove oldname from its original position
    args.name = oldname;
    args.len = oldlen;
    status = ForEachDirent(&args, cb_dir_force_unlink);
done:
    oldvn->RefRelease();
    return status;
}

mx_status_t VnodeMinfs::Link(const char* name, size_t len, fs::Vnode* _target) {
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

    VnodeMinfs* target = static_cast<VnodeMinfs*>(_target);
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

    args.ino = target->ino_;
    args.type = kMinfsTypeFile; // We can't hard link directories
    args.reclen = static_cast<uint32_t>(DirentSize(static_cast<uint8_t>(len)));
    if ((status = ForEachDirent(&args, cb_dir_append)) < 0) {
        return status;
    }

    // We have successfully added the vn to a new location. Increment the link count.
    target->inode_.link_count++;
    target->InodeSync(kMxFsSyncDefault);

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
