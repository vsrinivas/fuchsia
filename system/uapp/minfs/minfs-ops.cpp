// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <mxtl/algorithm.h>
#include <magenta/device/devmgr.h>

#ifdef __Fuchsia__
#include <magenta/syscalls.h>
#include <mxio/vfs.h>
#endif

#include "minfs-private.h"

#ifdef __Fuchsia__
static mx_status_t vmo_read_exact(mx_handle_t h, void* data, uint64_t offset, size_t len) {
    size_t actual;
    mx_status_t status = mx_vmo_read(h, data, offset, len, &actual);
    if (status != NO_ERROR) {
        return status;
    } else if (actual != len) {
        return ERR_IO;
    }
    return NO_ERROR;
}

static mx_status_t vmo_write_exact(mx_handle_t h, const void* data, uint64_t offset, size_t len) {
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

#define VNODE_IS_DIR(vn) (vn->inode.magic == kMinfsMagicDir)

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
        uint32_t bitblock_old = blk->GetKey() - info.abm_block;
        if (bitblock_old == bitblock) {
            // same block as before, nothing to do
            return blk;
        }
        // write previous block to disk
        const void* src = block_map.GetBlock(bitblock_old);
        memcpy(blk->data(), src, kMinfsBlockSize);
        bc->Put(blk, kBlockDirty);
    }
    return mxtl::RefPtr<BlockNode>(bc->Get(info.abm_block + bitblock));
}

void Minfs::BitmapBlockPut(const mxtl::RefPtr<BlockNode>& blk) {
    if (blk) {
        uint32_t bitblock = blk->GetKey() - info.abm_block;
        const void* src = block_map.GetBlock(bitblock);
        memcpy(blk->data(), src, kMinfsBlockSize);
        bc->Put(blk, kBlockDirty);
    }
}

static mx_status_t minfs_inode_destroy(vnode_t* vn) {
    minfs_inode_t inode;

    trace(MINFS, "inode_destroy() ino=%u\n", vn->ino);

    // save local copy, destroy inode on disk
    memcpy(&inode, &vn->inode, sizeof(inode));
    memset(&vn->inode, 0, sizeof(inode));
    minfs_sync_vnode(vn, kMxFsSyncDefault);
    return vn->fs->InoFree(inode, vn->ino);
}

// Delete all blocks (relative to a file) from "start" (inclusive) to the end of
// the file. Does not update mtime/atime.
static mx_status_t vn_blocks_shrink(vnode_t* vn, uint32_t start) {
    mxtl::RefPtr<BlockNode> bitmap_blk = nullptr;

    // release direct blocks
    for (unsigned bno = start; bno < kMinfsDirect; bno++) {
        if (vn->inode.dnum[bno] == 0) {
            continue;
        }
        if ((bitmap_blk = vn->fs->BitmapBlockGet(bitmap_blk, vn->inode.dnum[bno])) == nullptr) {
            return ERR_IO;
        }

        vn->fs->block_map.Clr(vn->inode.dnum[bno]);
        vn->inode.dnum[bno] = 0;
        vn->inode.block_count--;
        minfs_sync_vnode(vn, kMxFsSyncDefault);
    }

    const unsigned direct_per_indirect = kMinfsBlockSize / sizeof(uint32_t);

    // release indirect blocks
    for (unsigned indirect = 0; indirect < kMinfsIndirect; indirect++) {
        if (vn->inode.inum[indirect] == 0) {
            continue;
        }
        unsigned bno = kMinfsDirect + (indirect + 1) * direct_per_indirect;
        if (start > bno) {
            continue;
        }
        mxtl::RefPtr<BlockNode> blk = nullptr;
        if ((blk = vn->fs->bc->Get(vn->inode.inum[indirect])) == nullptr) {
            vn->fs->BitmapBlockPut(bitmap_blk);
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

            if ((bitmap_blk = vn->fs->BitmapBlockGet(bitmap_blk, entry[direct])) == nullptr) {
                vn->fs->bc->Put(blk, iflags);
                return ERR_IO;
            }
            vn->fs->block_map.Clr(entry[direct]);
            entry[direct] = 0;
            iflags = kBlockDirty;
            vn->inode.block_count--;
        }
        // only update the indirect block if an entry was deleted
        if (iflags & kBlockDirty) {
            minfs_sync_vnode(vn, kMxFsSyncDefault);
        }
        vn->fs->bc->Put(blk, iflags);

        if (delete_indirect)  {
            // release the direct block itself
            bitmap_blk = vn->fs->BitmapBlockGet(bitmap_blk, vn->inode.inum[indirect]);
            if (bitmap_blk == nullptr) {
                return ERR_IO;
            }
            vn->fs->block_map.Clr(vn->inode.inum[indirect]);
            vn->inode.inum[indirect] = 0;
            vn->inode.block_count--;
            minfs_sync_vnode(vn, kMxFsSyncDefault);
        }
    }

    vn->fs->BitmapBlockPut(bitmap_blk);
    return NO_ERROR;
}

#ifdef __Fuchsia__
// Read data from disk at block 'bno', into the 'nth' logical block of the file.
static mx_status_t vn_fill_block(vnode_t* vn, uint32_t n, uint32_t bno) {
    // TODO(smklein): read directly from block device into vmo; no need to copy
    // into an intermediate buffer.
    char bdata[kMinfsBlockSize];
    if (vn->fs->bc->Readblk(bno, bdata)) {
        return ERR_IO;
    }
    mx_status_t status = vmo_write_exact(vn->vmo, bdata, n * kMinfsBlockSize, kMinfsBlockSize);
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
static mx_status_t vn_init_vmo(vnode_t* vn) {
    if (vn->vmo != MX_HANDLE_INVALID) {
        return NO_ERROR;
    }

    mx_status_t status;
    if ((status = mx_vmo_create(mxtl::roundup(vn->inode.size, kMinfsBlockSize), 0, &vn->vmo)) != NO_ERROR) {
        error("Failed to initialize vmo; error: %d\n", status);
        return status;
    }

    // Initialize all direct blocks
    uint32_t bno;
    for (uint32_t d = 0; d < kMinfsDirect; d++) {
        if ((bno = vn->inode.dnum[d]) != 0) {
            if ((status = vn_fill_block(vn, d, bno)) != NO_ERROR) {
                error("Failed to fill bno %u; error: %d\n", bno, status);
                return status;
            }
        }
    }

    // Initialize all indirect blocks
    for (uint32_t i = 0; i < kMinfsIndirect; i++) {
        uint32_t ibno;
        mxtl::RefPtr<BlockNode> iblk;
        if ((ibno = vn->inode.inum[i]) != 0) {
            // TODO(smklein): Should there be a separate vmo for indirect blocks?
            if ((iblk = vn->fs->bc->Get(ibno)) == nullptr) {
                return ERR_IO;
            }
            uint32_t* ientry = static_cast<uint32_t*>(iblk->data());

            const uint32_t direct_per_indirect = kMinfsBlockSize / sizeof(uint32_t);
            for (uint32_t j = 0; j < direct_per_indirect; j++) {
                if ((bno = ientry[j]) != 0) {
                    uint32_t n = kMinfsDirect + i * direct_per_indirect + j;
                    if ((status = vn_fill_block(vn, n, bno)) != NO_ERROR) {
                        vn->fs->bc->Put(iblk, 0);
                        return status;
                    }
                }
            }
            vn->fs->bc->Put(iblk, 0);
        }
    }

    return NO_ERROR;
}
#endif

// Get the bno corresponding to the nth logical block within the file.
static mx_status_t vn_get_bno(vnode_t* vn, uint32_t n, uint32_t* bno, bool alloc) {
    uint32_t hint = 0;
    // direct blocks are simple... is there an entry in dnum[]?
    if (n < kMinfsDirect) {
        if (((*bno = vn->inode.dnum[n]) == 0) && alloc) {
            mx_status_t status = vn->fs->BlockNew(hint, bno, nullptr);
            if (status != NO_ERROR) {
                return status;
            }
            vn->inode.dnum[n] = *bno;
            vn->inode.block_count++;
            minfs_sync_vnode(vn, kMxFsSyncDefault);
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
    if ((ibno = vn->inode.inum[i]) == 0) {
        if (!alloc) {
            *bno = 0;
            return NO_ERROR;
        }
        // allocate a new indirect block
        mx_status_t status = vn->fs->BlockNew(0, &ibno, &iblk);
        if (status != NO_ERROR) {
            return status;
        }
        // record new indirect block in inode, note that we need to update
        vn->inode.block_count++;
        vn->inode.inum[i] = ibno;
        iflags = kBlockDirty;
    } else {
        if ((iblk = vn->fs->bc->Get(ibno)) == nullptr) {
            return ERR_IO;
        }
    }
    uint32_t* ientry = static_cast<uint32_t*>(iblk->data());

    if (((*bno = ientry[j]) == 0) && alloc) {
        // allocate a new block
        mx_status_t status = vn->fs->BlockNew(hint, bno, nullptr);
        if (status != NO_ERROR) {
            vn->fs->bc->Put(iblk, iflags);
            return status;
        }
        vn->inode.block_count++;
        ientry[j] = *bno;
        iflags = kBlockDirty;
    }

    // release indirect block, updating if necessary
    // and update the inode as well if we changed it
    vn->fs->bc->Put(iblk, iflags);
    if (iflags & kBlockDirty) {
        minfs_sync_vnode(vn, kMxFsSyncDefault);
    }

    return NO_ERROR;
}

// Immediately stop iterating over the directory.
#define DIR_CB_DONE 0
// Access the next direntry in the directory. Offsets updated.
#define DIR_CB_NEXT 1
// Identify that the direntry record was modified. Stop iterating.
#define DIR_CB_SAVE_SYNC 2

static mx_status_t _fs_read(vnode_t* vn, void* data, size_t len, size_t off, size_t* actual);
static mx_status_t _fs_write(vnode_t* vn, const void* data, size_t len, size_t off, size_t* actual);
static mx_status_t _fs_truncate(vnode_t* vn, size_t len);

static mx_status_t _fs_read_exact(vnode_t* vn, void* data, size_t len, size_t off) {
    size_t actual;
    mx_status_t status = _fs_read(vn, data, len, off, &actual);
    if (status != NO_ERROR) {
        return status;
    } else if (actual != len) {
        return ERR_IO;
    }
    return NO_ERROR;
}

static mx_status_t _fs_write_exact(vnode_t* vn, const void* data, size_t len, size_t off) {
    size_t actual;
    mx_status_t status = _fs_write(vn, data, len, off, &actual);
    if (status != NO_ERROR) {
        return status;
    } else if (actual != len) {
        return ERR_IO;
    }
    return NO_ERROR;
}

typedef struct dir_args {
    const char* name;
    size_t len;
    uint32_t ino;
    uint32_t type;
    uint32_t reclen;
} dir_args_t;

typedef struct de_off {
    size_t off;      // Offset in directory of current record
    size_t off_prev; // Offset in directory of previous record
} de_off_t;

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
static mx_status_t do_next_dirent(minfs_dirent_t* de, de_off_t* offs) {
    offs->off_prev = offs->off;
    offs->off += MinfsReclen(de, offs->off);
    return DIR_CB_NEXT;
}

static mx_status_t cb_dir_find(vnode_t* vndir, minfs_dirent_t* de, dir_args_t* args,
                               de_off_t* offs) {
    if ((de->ino != 0) && (de->namelen == args->len) &&
        (!memcmp(de->name, args->name, args->len))) {
        args->ino = de->ino;
        args->type = de->type;
        return DIR_CB_DONE;
    } else {
        return do_next_dirent(de, offs);
    }
}

static mx_status_t can_unlink(vnode_t* vn) {
    // directories must be empty (dirent_count == 2)
    if (VNODE_IS_DIR(vn)) {
        if (vn->inode.dirent_count != 2) {
            // if we have more than "." and "..", not empty, cannot unlink
            return ERR_BAD_STATE;
        } else if (vn->refcount > 1) {
            // if the target directory is open elsewhere, cannot unlink
            return ERR_BAD_STATE;
        }
    }
    return NO_ERROR;
}

static mx_status_t do_unlink(vnode_t* vndir, vnode_t* vn, minfs_dirent_t* de,
                             de_off_t* offs) {
    // Coalesce the current dirent with the previous/next dirent, if they
    // (1) exist and (2) are free.
    size_t off_prev = offs->off_prev;
    size_t off = offs->off;
    size_t off_next = off + MinfsReclen(de, off);
    minfs_dirent_t de_prev, de_next;

    // Read the direntries we're considering merging with.
    // Verify they are free and small enough to merge.
    size_t coalesced_size = MinfsReclen(de, off);
    // Coalesce with "next" first, so the kMinfsReclenLast bit can easily flow
    // back to "de" and "de_prev".
    if (!(de->reclen & kMinfsReclenLast)) {
        size_t len = MINFS_DIRENT_SIZE;
        mx_status_t status = _fs_read_exact(vndir, &de_next, len, off_next);
        if (status != NO_ERROR) {
            error("unlink: Failed to read next dirent\n");
            return status;
        } else if (validate_dirent(&de_next, len, off_next) != NO_ERROR) {
            error("unlink: Read invalid dirent\n");
            return ERR_IO;
        }
        if (de_next.ino == 0) {
            coalesced_size += MinfsReclen(&de_next, off_next);
            // If the next entry *was* last, then 'de' is now last.
            de->reclen |= (de_next.reclen & kMinfsReclenLast);
        }
    }
    if (off_prev != off) {
        size_t len = MINFS_DIRENT_SIZE;
        mx_status_t status = _fs_read_exact(vndir, &de_prev, len, off_prev);
        if (status != NO_ERROR) {
            error("unlink: Failed to read previous dirent\n");
            return status;
        } else if (validate_dirent(&de_prev, len, off_prev) != NO_ERROR) {
            error("unlink: Read invalid dirent\n");
            return ERR_IO;
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
    mx_status_t status = _fs_write_exact(vndir, de, MINFS_DIRENT_SIZE, off);
    if (status != NO_ERROR) {
        return status;
    }

    if (de->reclen & kMinfsReclenLast) {
        // Truncating the directory merely removed unused space; if it fails,
        // the directory contents are still valid.
        _fs_truncate(vndir, off + MINFS_DIRENT_SIZE);
    }

    // This effectively 'unlinks' the target node without deleting the direntry
    vn->inode.link_count--;
    vn_release(vn);

    // erase dirent (convert to empty entry), decrement dirent count
    vndir->inode.dirent_count--;
    minfs_sync_vnode(vndir, kMxFsSyncMtime);
    return DIR_CB_SAVE_SYNC;
}

// caller is expected to prevent unlink of "." or ".."
static mx_status_t cb_dir_unlink(vnode_t* vndir, minfs_dirent_t* de,
                                 dir_args_t* args, de_off_t* offs) {
    if ((de->ino == 0) || (args->len != de->namelen) ||
        memcmp(args->name, de->name, args->len)) {
        return do_next_dirent(de, offs);
    }

    vnode_t* vn;
    mx_status_t status;
    if ((status = vndir->fs->VnodeGet(&vn, de->ino)) < 0) {
        return status;
    }

    // If a directory was requested, then only try unlinking a directory
    if ((args->type == kMinfsTypeDir) && !VNODE_IS_DIR(vn)) {
        vn_release(vn);
        return ERR_NOT_DIR;
    }
    if ((status = can_unlink(vn)) < 0) {
        vn_release(vn);
        return status;
    }
    return do_unlink(vndir, vn, de, offs);
}

// same as unlink, but do not validate vnode
static mx_status_t cb_dir_force_unlink(vnode_t* vndir, minfs_dirent_t* de,
                                       dir_args_t* args, de_off_t* offs) {
    if ((de->ino == 0) || (args->len != de->namelen) ||
        memcmp(args->name, de->name, args->len)) {
        return do_next_dirent(de, offs);
    }

    vnode_t* vn;
    mx_status_t status;
    if ((status = vndir->fs->VnodeGet(&vn, de->ino)) < 0) {
        return status;
    }
    return do_unlink(vndir, vn, de, offs);
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
static mx_status_t cb_dir_attempt_rename(vnode_t* vndir, minfs_dirent_t* de,
                                         dir_args_t* args, de_off_t* offs) {
    if ((de->ino == 0) || (args->len != de->namelen) ||
        memcmp(args->name, de->name, args->len)) {
        return do_next_dirent(de, offs);
    }

    vnode_t* vn;
    mx_status_t status;
    if ((status = vndir->fs->VnodeGet(&vn, de->ino)) < 0) {
        return status;
    } else if (args->ino == vn->ino) {
        // cannot rename node to itself
        vn_release(vn);
        return ERR_BAD_STATE;
    } else if (args->type != de->type) {
        // cannot rename directory to file (or vice versa)
        vn_release(vn);
        return ERR_BAD_STATE;
    } else if ((status = can_unlink(vn)) < 0) {
        // if we cannot unlink the target, we cannot rename the target
        vn_release(vn);
        return status;
    }

    vn->inode.link_count--;
    vn_release(vn);

    de->ino = args->ino;
    status = _fs_write_exact(vndir, de, DirentSize(de->namelen), offs->off);
    if (status != NO_ERROR) {
        return status;
    }
    return DIR_CB_SAVE_SYNC;
}

static mx_status_t cb_dir_update_inode(vnode_t* vndir, minfs_dirent_t* de,
                                       dir_args_t* args, de_off_t* offs) {
    if ((de->ino == 0) || (args->len != de->namelen) ||
        memcmp(args->name, de->name, args->len)) {
        return do_next_dirent(de, offs);
    }

    de->ino = args->ino;
    mx_status_t status = _fs_write_exact(vndir, de, DirentSize(de->namelen), offs->off);
    if (status != NO_ERROR) {
        return status;
    }
    return DIR_CB_SAVE_SYNC;
}

static mx_status_t fill_dirent(vnode_t* vndir, minfs_dirent_t* de,
                               dir_args_t* args, size_t off) {
    de->ino = args->ino;
    de->type = static_cast<uint8_t>(args->type);
    de->namelen = static_cast<uint8_t>(args->len);
    memcpy(de->name, args->name, args->len);
    vndir->inode.dirent_count++;
    mx_status_t status = _fs_write_exact(vndir, de, DirentSize(de->namelen), off);
    if (status != NO_ERROR) {
        return status;
    }
    return DIR_CB_SAVE_SYNC;
}

static mx_status_t cb_dir_append(vnode_t* vndir, minfs_dirent_t* de,
                                 dir_args_t* args, de_off_t* offs) {
    uint32_t reclen = static_cast<uint32_t>(MinfsReclen(de, offs->off));
    if (de->ino == 0) {
        // empty entry, do we fit?
        if (args->reclen > reclen) {
            return do_next_dirent(de, offs);
        }
        return fill_dirent(vndir, de, args, offs->off);
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
        mx_status_t status = _fs_write_exact(vndir, de, DirentSize(de->namelen), offs->off);
        if (status != NO_ERROR) {
            return status;
        }
        offs->off += size;
        // create new entry in the remaining space
        de = (minfs_dirent_t*) ((uintptr_t)de + size);
        char data[kMinfsMaxDirentSize];
        de = (minfs_dirent_t*) data;
        de->reclen = extra | (was_last_record ? kMinfsReclenLast : 0);
        return fill_dirent(vndir, de, args, offs->off);
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
//  'args': Additional arguments plubmed through vn_dir_for_each
//  'offs': Offset info about where in the directory this direntry is located.
//          Since 'func' may create / remove surrounding dirents, it is responsible for
//          updating the offset information to access the next dirent.
static mx_status_t vn_dir_for_each(vnode_t* vn, dir_args_t* args,
                                   mx_status_t (*func)(vnode_t*, minfs_dirent_t*,
                                                       dir_args_t*, de_off_t* offs)) {
    char data[kMinfsMaxDirentSize];
    minfs_dirent_t* de = (minfs_dirent_t*) data;
    de_off_t offs = {
        .off = 0,
        .off_prev = 0,
    };
    while (offs.off + MINFS_DIRENT_SIZE < kMinfsMaxDirectorySize) {
        trace(MINFS, "Reading dirent at offset %zd\n", offs.off);
        size_t r;
        mx_status_t status = _fs_read(vn, data, kMinfsMaxDirentSize, offs.off, &r);
        if (status != NO_ERROR) {
            return status;
        } else if ((status = validate_dirent(de, r, offs.off)) != NO_ERROR) {
            return status;
        }

        switch ((status = func(vn, de, args, &offs))) {
        case DIR_CB_NEXT:
            break;
        case DIR_CB_SAVE_SYNC:
            vn->inode.seq_num++;
            minfs_sync_vnode(vn, kMxFsSyncMtime);
            return NO_ERROR;
        case DIR_CB_DONE:
        default:
            return status;
        }
    }
    return ERR_NOT_FOUND;
}

static void fs_release(vnode_t* vn) {
    trace(MINFS, "minfs_release() vn=%p(#%u)%s\n", vn, vn->ino,
          vn->inode.link_count ? "" : " link-count is zero");
    if (vn->inode.link_count == 0) {
        minfs_inode_destroy(vn);
    }
    list_delete(&vn->hashnode);
#ifdef __Fuchsia__
    mx_handle_close(vn->vmo);
#endif
    free(vn);
}

static mx_status_t fs_open(vnode_t** _vn, uint32_t flags) {
    vnode_t* vn = *_vn;
    trace(MINFS, "minfs_open() vn=%p(#%u)\n", vn, vn->ino);
    if ((flags & O_DIRECTORY) && !VNODE_IS_DIR(vn)) {
        return ERR_NOT_DIR;
    }
    vn_acquire(vn);
    return NO_ERROR;
}

static mx_status_t fs_close(vnode_t* vn) {
    trace(MINFS, "minfs_close() vn=%p(#%u)\n", vn, vn->ino);
    vn_release(vn);
    return NO_ERROR;
}

static ssize_t fs_read(vnode_t* vn, void* data, size_t len, size_t off) {
    trace(MINFS, "minfs_read() vn=%p(#%u) len=%zd off=%zd\n", vn, vn->ino, len, off);
    if (VNODE_IS_DIR(vn)) {
        return ERR_NOT_FILE;
    }
    size_t r;
    mx_status_t status = _fs_read(vn, data, len, off, &r);
    if (status != NO_ERROR) {
        return status;
    }
    return r;
}

// Internal read. Usable on directories.
static mx_status_t _fs_read(vnode_t* vn, void* data, size_t len, size_t off, size_t* actual) {
    // clip to EOF
    if (off >= vn->inode.size) {
        *actual = 0;
        return NO_ERROR;
    }
    if (len > (vn->inode.size - off)) {
        len = vn->inode.size - off;
    }

    mx_status_t status;
#ifdef __Fuchsia__
    if ((status = vn_init_vmo(vn)) != NO_ERROR) {
        return status;
    } else if ((status = mx_vmo_read(vn->vmo, data, off, len, actual)) != NO_ERROR) {
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
        if ((status = vn_get_bno(vn, n, &bno, false)) != NO_ERROR) {
            return status;
        }
        if (bno != 0) {
            char bdata[kMinfsBlockSize];
            if (vn->fs->bc->Readblk(bno, bdata)) {
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

static ssize_t fs_write(vnode_t* vn, const void* data, size_t len, size_t off) {
    trace(MINFS, "minfs_write() vn=%p(#%u) len=%zd off=%zd\n", vn, vn->ino, len, off);
    if (VNODE_IS_DIR(vn)) {
        return ERR_NOT_FILE;
    }
    size_t actual;
    mx_status_t status = _fs_write(vn, data, len, off, &actual);
    if (status != NO_ERROR) {
        return status;
    }
    return actual;
}

// Internal write. Usable on directories.
static mx_status_t _fs_write(vnode_t* vn, const void* data, size_t len, size_t off, size_t* actual) {
    if (len == 0) {
        *actual = 0;
        return NO_ERROR;
    }

    mx_status_t status;
#ifdef __Fuchsia__
    if ((status = vn_init_vmo(vn)) != NO_ERROR) {
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
        if ((xfer_off + xfer) > vn->inode.size) {
            size_t new_size = xfer_off + xfer;
            if ((status = mx_vmo_set_size(vn->vmo, mxtl::roundup(new_size, kMinfsBlockSize))) != NO_ERROR) {
                goto done;
            }
            vn->inode.size = static_cast<uint32_t>(new_size);
        }

        // TODO(smklein): If a failure occurs after writing to the VMO, but
        // before updating the data to disk, then our in-memory representation
        // of the file may not be consistent with the on-disk representation of
        // the file. As a consequence, an error is returned (ERR_IO) rather than
        // doing a partial read.

        // Update this block of the in-memory VMO
        if ((status = vmo_write_exact(vn->vmo, data, xfer_off, xfer)) != NO_ERROR) {
            return ERR_IO;
        }

        // Update this block on-disk
        char bdata[kMinfsBlockSize];
        // TODO(smklein): Can we write directly from the VMO to the block device,
        // preventing the need for a 'bdata' variable?
        if (xfer != kMinfsBlockSize) {
            if (vmo_read_exact(vn->vmo, bdata, n * kMinfsBlockSize, kMinfsBlockSize) != NO_ERROR) {
                return ERR_IO;
            }
        }
        const void* wdata = (xfer != kMinfsBlockSize) ? bdata : data;
        uint32_t bno;
        if ((status = vn_get_bno(vn, n, &bno, true)) != NO_ERROR) {
            return status;
        }
        assert(bno != 0);
        if (vn->fs->bc->Writeblk(bno, wdata)) {
            return ERR_IO;
        }
#else
        uint32_t bno;
        if ((status = vn_get_bno(vn, n, &bno, true)) != NO_ERROR) {
            goto done;
        }
        assert(bno != 0);
        char wdata[kMinfsBlockSize];
        if (vn->fs->bc->Readblk(bno, wdata)) {
            return ERR_IO;
        }
        memcpy(wdata + adjust, data, xfer);
        if (vn->fs->bc->Writeblk(bno, wdata)) {
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
    if ((off + len) > vn->inode.size) {
        vn->inode.size = static_cast<uint32_t>(off + len);
    }

    minfs_sync_vnode(vn, kMxFsSyncMtime);  // writes always update mtime
    *actual = len;
    return NO_ERROR;
}

static mx_status_t fs_lookup(vnode_t* vn, vnode_t** out, const char* name, size_t len) {
    trace(MINFS, "minfs_lookup() vn=%p(#%u) name='%.*s'\n", vn, vn->ino, (int)len, name);
    assert(len <= kMinfsMaxNameSize);
    assert(memchr(name, '/', len) == NULL);

    if (!VNODE_IS_DIR(vn)) {
        error("not directory\n");
        return ERR_NOT_SUPPORTED;
    }
    dir_args_t args = dir_args_t();
    args.name = name;
    args.len = len;
    mx_status_t status;
    if ((status = vn_dir_for_each(vn, &args, cb_dir_find)) < 0) {
        return status;
    }
    if ((status = vn->fs->VnodeGet(&vn, args.ino)) < 0) {
        return status;
    }
    *out = vn;
    return NO_ERROR;
}

static mx_status_t fs_getattr(vnode_t* vn, vnattr_t* a) {
    trace(MINFS, "minfs_getattr() vn=%p(#%u)\n", vn, vn->ino);
    a->inode = vn->ino;
    a->size = vn->inode.size;
    a->mode = DTYPE_TO_VTYPE(MinfsMagicType(vn->inode.magic));
    a->create_time = vn->inode.create_time;
    a->modify_time = vn->inode.modify_time;
    return NO_ERROR;
}

static mx_status_t fs_setattr(vnode_t* vn, vnattr_t* a) {
    int dirty = 0;
    trace(MINFS, "minfs_setattr() vn=%p(#%u)\n", vn, vn->ino);
    if ((a->valid & ~(ATTR_CTIME|ATTR_MTIME)) != 0) {
        return ERR_NOT_SUPPORTED;
    }
    if ((a->valid & ATTR_CTIME) != 0) {
        vn->inode.create_time = a->create_time;
        dirty = 1;
    }
    if ((a->valid & ATTR_MTIME) != 0) {
        vn->inode.modify_time = a->modify_time;
        dirty = 1;
    }
    if (dirty) {
        // write to disk, but don't overwrite the time
        minfs_sync_vnode(vn, kMxFsSyncDefault);
    }
    return NO_ERROR;
}

#define DIRCOOKIE_FLAG_USED 1
#define DIRCOOKIE_FLAG_ERROR 2

typedef struct dircookie {
    uint32_t flags;  // Identifies the state of the dircookie
    size_t off;      // Offset into directory
    uint32_t seqno;  // inode seq no
} dircookie_t;

static mx_status_t fs_readdir(vnode_t* vn, void* cookie, void* dirents, size_t len) {
    trace(MINFS, "minfs_readdir() vn=%p(#%u) cookie=%p len=%zd\n", vn, vn->ino, cookie, len);
    dircookie_t* dc = reinterpret_cast<dircookie_t*>(cookie);
    vdirent_t* out = reinterpret_cast<vdirent_t*>(dirents);

    if (!VNODE_IS_DIR(vn)) {
        return ERR_NOT_SUPPORTED;
    }

    size_t off;
    char data[kMinfsMaxDirentSize];
    minfs_dirent_t* de = (minfs_dirent_t*) data;
    if (dc->flags & DIRCOOKIE_FLAG_ERROR) {
        return ERR_IO;
    } else if (dc->flags & DIRCOOKIE_FLAG_USED) {
        if (dc->seqno != vn->inode.seq_num) {
            // directory has been modified
            // stop returning entries
            trace(MINFS, "minfs_readdir() Directory modified since readdir started\n");
            goto fail;
        }
        off = dc->off;
    } else {
        off = 0;
    }

    size_t r;
    while (off + MINFS_DIRENT_SIZE < kMinfsMaxDirectorySize) {
        mx_status_t status = _fs_read(vn, de, kMinfsMaxDirentSize, off, &r);
        if (status != NO_ERROR) {
            goto fail;
        } else if (validate_dirent(de, r, off) != NO_ERROR) {
            goto fail;
        }

        if (de->ino) {
            mx_status_t status;
            size_t len_remaining = len - (size_t)((uintptr_t)out - (uintptr_t)dirents);
            if ((status = vfs_fill_dirent(out, len_remaining, de->name,
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
    dc->flags |= DIRCOOKIE_FLAG_USED;
    dc->off = off;
    dc->seqno = vn->inode.seq_num;
    r = static_cast<size_t>(((uintptr_t) out - (uintptr_t)dirents));
    assert(r <= len); // Otherwise, we're overflowing the input buffer.
    return static_cast<mx_status_t>(r);

fail:
    // mark dircookie so further reads also fail
    dc->off = 0;
    dc->flags |= DIRCOOKIE_FLAG_ERROR;
    return ERR_IO;
}

static mx_status_t fs_create(vnode_t* vndir, vnode_t** out,
                             const char* name, size_t len, uint32_t mode) {
    trace(MINFS, "minfs_create() vn=%p(#%u) name='%.*s' mode=%#x\n",
          vndir, vndir->ino, (int)len, name, mode);
    assert(len <= kMinfsMaxNameSize);
    assert(memchr(name, '/', len) == NULL);
    if (!VNODE_IS_DIR(vndir)) {
        return ERR_NOT_SUPPORTED;
    }

    dir_args_t args = dir_args_t();
    args.name = name;
    args.len = len;
    // ensure file does not exist
    mx_status_t status;
    if ((status = vn_dir_for_each(vndir, &args, cb_dir_find)) != ERR_NOT_FOUND) {
        return ERR_ALREADY_EXISTS;
    }

    // creating a directory?
    uint32_t type = S_ISDIR(mode) ? kMinfsTypeDir : kMinfsTypeFile;

    // mint a new inode and vnode for it
    vnode_t* vn;
    if ((status = vndir->fs->VnodeNew(&vn, type)) < 0) { // vn refcount +1
        return status;
    }

    // If the new node is a directory, fill it with '.' and '..'.
    if (type == kMinfsTypeDir) {
        char bdata[DirentSize(1) + DirentSize(2)];
        minfs_dir_init(bdata, vn->ino, vndir->ino);
        size_t expected = DirentSize(1) + DirentSize(2);
        if (_fs_write_exact(vn, bdata, expected, 0) != NO_ERROR) {
            fs_release(vn); // vn refcount +0
            return ERR_IO;
        }
        vn->inode.dirent_count = 2;
        minfs_sync_vnode(vn, kMxFsSyncDefault);
    }

    // add directory entry for the new child node
    args.ino = vn->ino;
    args.type = type;
    args.reclen = static_cast<uint32_t>(DirentSize(static_cast<uint8_t>(len)));
    if ((status = vn_dir_for_each(vndir, &args, cb_dir_append)) < 0) {
        fs_release(vn); // vn refcount +0
        return status;
    }

    *out = vn; // vn refcount returned as +1
    return NO_ERROR;
}

static ssize_t fs_ioctl(vnode_t* vn, uint32_t op, const void* in_buf,
                        size_t in_len, void* out_buf, size_t out_len) {
    switch (op) {
        case IOCTL_DEVMGR_UNMOUNT_FS: {
            mx_status_t status = vn->ops->sync(vn);
            if (status != NO_ERROR) {
                error("minfs unmount failed to sync; unmounting anyway: %d\n", status);
            }
            return vn->fs->Unmount();
        }
        default: {
            return ERR_NOT_SUPPORTED;
        }
    }
}

static mx_status_t fs_unlink(vnode_t* vn, const char* name, size_t len, bool must_be_dir) {
    trace(MINFS, "minfs_unlink() vn=%p(#%u) name='%.*s'\n", vn, vn->ino, (int)len, name);
    assert(len <= kMinfsMaxNameSize);
    assert(memchr(name, '/', len) == NULL);
    if (!VNODE_IS_DIR(vn)) {
        return ERR_NOT_SUPPORTED;
    }
    if ((len == 1) && (name[0] == '.')) {
        return ERR_BAD_STATE;
    }
    if ((len == 2) && (name[0] == '.') && (name[1] == '.')) {
        return ERR_BAD_STATE;
    }
    dir_args_t args = dir_args_t();
    args.name = name;
    args.len = len;
    args.type = must_be_dir ? kMinfsTypeDir : 0;
    return vn_dir_for_each(vn, &args, cb_dir_unlink);
}

static mx_status_t fs_truncate(vnode_t* vn, size_t len) {
    if (VNODE_IS_DIR(vn)) {
        return ERR_NOT_FILE;
    }

    return _fs_truncate(vn, len);
}

static mx_status_t _fs_truncate(vnode_t* vn, size_t len) {
    mx_status_t r = 0;
#ifdef __Fuchsia__
    if (vn_init_vmo(vn) != NO_ERROR) {
        return ERR_IO;
    }
#endif

    if (len < vn->inode.size) {
        // Truncate should make the file shorter
        size_t bno = vn->inode.size / kMinfsBlockSize;
        size_t trunc_bno = len / kMinfsBlockSize;

        // Truncate to the nearest block
        if (trunc_bno <= bno) {
            uint32_t start_bno = static_cast<uint32_t>((len % kMinfsBlockSize == 0) ?
                                                       trunc_bno : trunc_bno + 1);
            if ((r = vn_blocks_shrink(vn, start_bno)) < 0) {
                return r;
            }

            if (start_bno * kMinfsBlockSize < vn->inode.size) {
                vn->inode.size = start_bno * kMinfsBlockSize;
            }
        }

        // Write zeroes to the rest of the remaining block, if it exists
        if (len < vn->inode.size) {
            char bdata[kMinfsBlockSize];
            uint32_t bno;
            if (vn_get_bno(vn, static_cast<uint32_t>(len / kMinfsBlockSize),
                           &bno, false) != NO_ERROR) {
                return ERR_IO;
            }
            if (bno != 0) {
                size_t adjust = len % kMinfsBlockSize;
#ifdef __Fuchsia__
                if ((r = vmo_read_exact(vn->vmo, bdata, len - adjust, adjust)) != NO_ERROR) {
                    return ERR_IO;
                }
                memset(bdata + adjust, 0, kMinfsBlockSize - adjust);

                // TODO(smklein): Remove this write when shrinking VMO size
                // automatically sets partial pages to zero.
                if ((r = vmo_write_exact(vn->vmo, bdata, len - adjust, kMinfsBlockSize)) != NO_ERROR) {
                    return ERR_IO;
                }
#else
                if (vn->fs->bc->Readblk(bno, bdata)) {
                    return ERR_IO;
                }
                memset(bdata + adjust, 0, kMinfsBlockSize - adjust);
#endif

                if (vn->fs->bc->Writeblk(bno, bdata)) {
                    return ERR_IO;
                }
            }
        }
        vn->inode.size = static_cast<uint32_t>(len);
        minfs_sync_vnode(vn, kMxFsSyncMtime);
    } else if (len > vn->inode.size) {
        // Truncate should make the file longer, filled with zeroes.
        if (kMinfsMaxFileSize < len) {
            return ERR_INVALID_ARGS;
        }
        char zero = 0;
        if ((r = _fs_write_exact(vn, &zero, 1, len - 1)) != NO_ERROR) {
            return r;
        }
    }

#ifdef __Fuchsia__
    if ((r = mx_vmo_set_size(vn->vmo, mxtl::roundup(len, kMinfsBlockSize))) != NO_ERROR) {
        return r;
    }
#endif

    return NO_ERROR;
}

// verify that the 'newdir' inode is not a subdirectory of the source.
static mx_status_t check_not_subdirectory(vnode_t* src, vnode_t* newdir) {
    vnode_t* vn = newdir;
    vnode_t* out = nullptr;
    mx_status_t status = NO_ERROR;
    // Acquire vn here so this function remains cleanly idempotent with respect
    // to refcounts. 'newdir' and all ancestors (until an exit condition is
    // reached) will be acquired once and released once.
    vn_acquire(vn);
    while (vn->ino != kMinfsRootIno) {
        if (vn->ino == src->ino) {
            status = ERR_INVALID_ARGS;
            break;
        }

        if ((status = fs_lookup(vn, &out, "..", 2)) < 0) {
            break;
        }
        vn_release(vn);
        vn = out;
    }
    vn_release(vn);
    return status;
}

static mx_status_t fs_rename(vnode_t* olddir, vnode_t* newdir,
                             const char* oldname, size_t oldlen,
                             const char* newname, size_t newlen,
                             bool src_must_be_dir, bool dst_must_be_dir) {
    trace(MINFS, "minfs_rename() olddir=%p(#%u) newdir=%p(#%u) oldname='%.*s' newname='%.*s'\n",
          olddir, olddir->ino, newdir, newdir->ino, (int)oldlen, oldname, (int)newlen, newname);
    assert(oldlen <= kMinfsMaxNameSize);
    assert(memchr(oldname, '/', oldlen) == NULL);
    assert(newlen <= kMinfsMaxNameSize);
    assert(memchr(newname, '/', newlen) == NULL);
    // ensure that the vnodes containin oldname and newname are directories
    if (!(VNODE_IS_DIR(olddir) && VNODE_IS_DIR(newdir)))
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
    vnode_t* oldvn = nullptr;
    // acquire the 'oldname' node (it must exist)
    dir_args_t args = dir_args_t();
    args.name = oldname;
    args.len = oldlen;
    if ((status = vn_dir_for_each(olddir, &args, cb_dir_find)) < 0) {
        return status;
    } else if ((status = olddir->fs->VnodeGet(&oldvn, args.ino)) < 0) {
        return status;
    } else if ((status = check_not_subdirectory(oldvn, newdir)) < 0) {
        goto done;
    }

    // If either the 'src' or 'dst' must be directories, BOTH of them must be directories.
    if (!VNODE_IS_DIR(oldvn) && (src_must_be_dir || dst_must_be_dir)) {
        status = ERR_NOT_DIR;
        goto done;
    }

    // if the entry for 'newname' exists, make sure it can be replaced by
    // the vnode behind 'oldname'.
    args.name = newname;
    args.len = newlen;
    args.ino = oldvn->ino;
    args.type = VNODE_IS_DIR(oldvn) ? kMinfsTypeDir : kMinfsTypeFile;
    status = vn_dir_for_each(newdir, &args, cb_dir_attempt_rename);
    if (status == ERR_NOT_FOUND) {
        // if 'newname' does not exist, create it
        args.reclen = static_cast<uint32_t>(DirentSize(static_cast<uint8_t>(newlen)));
        if ((status = vn_dir_for_each(newdir, &args, cb_dir_append)) < 0) {
            goto done;
        }
        status = NO_ERROR;
    } else if (status != NO_ERROR) {
        goto done;
    }

    // update the oldvn's entry for '..' if (1) it was a directory, and (2) it
    // moved to a new directory
    if ((args.type == kMinfsTypeDir) && (olddir->ino != newdir->ino)) {
        vnode_t* vn;
        if ((status = fs_lookup(newdir, &vn, newname, newlen)) < 0) {
            goto done;
        }
        args.name = "..";
        args.len = 2;
        args.ino = newdir->ino;
        if ((status = vn_dir_for_each(vn, &args, cb_dir_update_inode)) < 0) {
            vn_release(vn);
            goto done;
        }
        vn_release(vn);
    }

    // at this point, the oldvn exists with multiple names (or the same name in
    // different directories)
    oldvn->inode.link_count++;

    // finally, remove oldname from its original position
    args.name = oldname;
    args.len = oldlen;
    status = vn_dir_for_each(olddir, &args, cb_dir_force_unlink);
done:
    vn_release(oldvn);
    return status;
}

static mx_status_t fs_sync(vnode_t* vn) {
    return vn->fs->bc->Sync();
}

vnode_ops_t minfs_ops = {
    .release = fs_release,
    .open = fs_open,
    .close = fs_close,
    .read = fs_read,
    .write = fs_write,
    .lookup = fs_lookup,
    .getattr = fs_getattr,
    .setattr = fs_setattr,
    .readdir = fs_readdir,
    .create = fs_create,
    .ioctl = fs_ioctl,
    .unlink = fs_unlink,
    .truncate = fs_truncate,
    .rename = fs_rename,
    .sync = fs_sync,
};
