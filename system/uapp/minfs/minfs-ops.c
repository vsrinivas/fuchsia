// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <magenta/device/devmgr.h>
#include <mxio/vfs.h>

#include "minfs-private.h"

//TODO: better bitmap block read/write functions

// Allocate a new data block from the block bitmap.
// Return the underlying block (obtained via bcache_get()).
// If hint is nonzero it indicates which block number
// to start the search for free blocks from.
block_t* minfs_new_block(minfs_t* fs, uint32_t hint, uint32_t* out_bno, void** bdata) {
    uint32_t bno = bitmap_alloc(&fs->block_map, hint);
    if ((bno == BITMAP_FAIL) && (hint != 0)) {
        bno = bitmap_alloc(&fs->block_map, 0);
    }
    if (bno == BITMAP_FAIL) {
        return NULL;
    }

    // obtain the in-memory bitmap block
    uint32_t bmbno;
    void *bmdata = minfs_bitmap_block(&fs->block_map, &bmbno, bno);

    // obtain the block of the alloc bitmap we need
    block_t* block_abm;
    void* bdata_abm;
    if ((block_abm = bcache_get(fs->bc, fs->info.abm_block + bmbno, &bdata_abm)) == NULL) {
        bitmap_clr(&fs->block_map, bno);
        return NULL;
    }

    // obtain the block we're allocating
    block_t* block;
    if ((block = bcache_get_zero(fs->bc, bno, bdata)) == NULL) {
        bitmap_clr(&fs->block_map, bno);
        bcache_put(fs->bc, block_abm, 0);
        return NULL;
    }

    // commit the bitmap
    memcpy(bdata_abm, bmdata, MINFS_BLOCK_SIZE);
    bcache_put(fs->bc, block_abm, BLOCK_DIRTY);
    *out_bno = bno;
    return block;
}

typedef struct {
    block_t* blk;
    uint32_t bno;
    void* data;
} gbb_ctxt_t;

// helper for updating many bitmap entries
// if the next entry is in the same block, defer
// write until a different block is needed
static mx_status_t get_bitmap_block(minfs_t* fs, gbb_ctxt_t* gbb, uint32_t n) {
    uint32_t bno = n / MINFS_BLOCK_BITS;
    if (gbb->blk) {
        if (gbb->bno == bno) {
            // same block as before, nothing to do
            return NO_ERROR;
        }
        // write previous block to disk
        memcpy(gbb->data, bitmap_data(&fs->block_map) + gbb->bno * MINFS_BLOCK_SIZE, MINFS_BLOCK_SIZE);
        bcache_put(fs->bc, gbb->blk, BLOCK_DIRTY);
    }
    gbb->bno = bno;
    if ((gbb->blk = bcache_get_zero(fs->bc, fs->info.abm_block + bno, &gbb->data)) == NULL) {
        return ERR_IO;
    } else {
        return NO_ERROR;
    }
}

static void put_bitmap_block(minfs_t* fs, gbb_ctxt_t* gbb) {
    if (gbb->blk) {
        memcpy(gbb->data, bitmap_data(&fs->block_map) + gbb->bno * MINFS_BLOCK_SIZE, MINFS_BLOCK_SIZE);
        bcache_put(fs->bc, gbb->blk, BLOCK_DIRTY);
    }
}

static mx_status_t minfs_inode_destroy(vnode_t* vn) {
    mx_status_t status;
    minfs_inode_t inode;
    gbb_ctxt_t gbb;
    memset(&gbb, 0, sizeof(gbb));

    trace(MINFS, "inode_destroy() ino=%u\n", vn->ino);

    // save local copy, destroy inode on disk
    memcpy(&inode, &vn->inode, sizeof(inode));
    memset(&vn->inode, 0, sizeof(inode));
    minfs_sync_vnode(vn, MX_FS_SYNC_DEFAULT);
    minfs_ino_free(vn->fs, vn->ino);

    // release all direct blocks
    for (unsigned n = 0; n < MINFS_DIRECT; n++) {
        if (inode.dnum[n] == 0) {
            continue;
        }
        if ((status = get_bitmap_block(vn->fs, &gbb, inode.dnum[n])) < 0) {
            return status;
        }
        bitmap_clr(&vn->fs->block_map, inode.dnum[n]);
    }

    // release all indirect blocks
    for (unsigned n = 0; n < MINFS_INDIRECT; n++) {
        if (inode.inum[n] == 0) {
            continue;
        }
        uint32_t* entry;
        block_t* blk;
        if ((blk = bcache_get(vn->fs->bc, inode.inum[n], (void**) &entry)) == NULL) {
            put_bitmap_block(vn->fs, &gbb);
            return ERR_IO;
        }
        // release the blocks pointed at by the entries in the indirect block
        for (unsigned m = 0; m < (MINFS_BLOCK_SIZE / sizeof(uint32_t)); m++) {
            if (entry[m] == 0) {
                continue;
            }
            if ((status = get_bitmap_block(vn->fs, &gbb, entry[m])) < 0) {
                put_bitmap_block(vn->fs, &gbb);
                return status;
            }
            bitmap_clr(&vn->fs->block_map, entry[m]);
        }
        bcache_put(vn->fs->bc, blk, 0);
        // release the direct block itself
        if ((status = get_bitmap_block(vn->fs, &gbb, inode.inum[n])) < 0) {
            return status;
        }
        bitmap_clr(&vn->fs->block_map, inode.inum[n]);
    }

    put_bitmap_block(vn->fs, &gbb);
    return NO_ERROR;
}

// Delete all blocks (relative to a file) from "start" (inclusive) to the end of
// the file. Does not update mtime/atime.
static mx_status_t vn_blocks_shrink(vnode_t* vn, uint32_t start) {
    mx_status_t status;
    gbb_ctxt_t gbb;
    memset(&gbb, 0, sizeof(gbb));

    // release direct blocks
    for (unsigned bno = start; bno < MINFS_DIRECT; bno++) {
        if (vn->inode.dnum[bno] == 0) {
            continue;
        }
        if ((status = get_bitmap_block(vn->fs, &gbb, vn->inode.dnum[bno])) < 0) {
            return status;
        }

        bitmap_clr(&vn->fs->block_map, vn->inode.dnum[bno]);
        vn->inode.dnum[bno] = 0;
        vn->inode.block_count--;
        minfs_sync_vnode(vn, MX_FS_SYNC_DEFAULT);
    }

    const unsigned direct_per_indirect = MINFS_BLOCK_SIZE / sizeof(uint32_t);

    // release indirect blocks
    for (unsigned indirect = 0; indirect < MINFS_INDIRECT; indirect++) {
        if (vn->inode.inum[indirect] == 0) {
            continue;
        }
        unsigned bno = MINFS_DIRECT + (indirect + 1) * direct_per_indirect;
        if (start > bno) {
            continue;
        }
        uint32_t* entry;
        block_t* blk;
        if ((blk = bcache_get(vn->fs->bc, vn->inode.inum[indirect], (void**) &entry)) == NULL) {
            put_bitmap_block(vn->fs, &gbb);
            return ERR_IO;
        }
        uint32_t iflags = 0;
        bool delete_indirect = true; // can we delete the indirect block?
        // release the blocks pointed at by the entries in the indirect block
        for (unsigned direct = 0; direct < direct_per_indirect; direct++) {
            if (entry[direct] == 0) {
                continue;
            }
            unsigned bno = MINFS_DIRECT + indirect * direct_per_indirect + direct;
            if (start > bno) {
                // This is a valid entry which exists in the indirect block
                // BEFORE our truncation point. Don't delete it, and don't
                // delete the indirect block.
                delete_indirect = false;
                continue;
            }

            if ((status = get_bitmap_block(vn->fs, &gbb, entry[direct])) < 0) {
                put_bitmap_block(vn->fs, &gbb);
                bcache_put(vn->fs->bc, blk, iflags);
                return status;
            }
            bitmap_clr(&vn->fs->block_map, entry[direct]);
            entry[direct] = 0;
            iflags = BLOCK_DIRTY;
            vn->inode.block_count--;
        }
        // only update the indirect block if an entry was deleted
        if (iflags & BLOCK_DIRTY) {
            minfs_sync_vnode(vn, MX_FS_SYNC_DEFAULT);
        }
        bcache_put(vn->fs->bc, blk, iflags);

        if (delete_indirect)  {
            // release the direct block itself
            if ((status = get_bitmap_block(vn->fs, &gbb, vn->inode.inum[indirect])) < 0) {
                return status;
            }
            bitmap_clr(&vn->fs->block_map, vn->inode.inum[indirect]);
            vn->inode.inum[indirect] = 0;
            vn->inode.block_count--;
            minfs_sync_vnode(vn, MX_FS_SYNC_DEFAULT);
        }
    }

    put_bitmap_block(vn->fs, &gbb);
    return NO_ERROR;
}

// Obtain the nth block of a vnode.
// If alloc is true, allocate that block if it doesn't already exist.
static block_t* vn_get_block(vnode_t* vn, uint32_t n, void** bdata, bool alloc) {
#if 0
    uint32_t hint = ((vn->fs->info.block_count - vn->fs->info.dat_block) / 256) * (vn->ino % 256);
#else
    uint32_t hint = 0;
#endif
    // direct blocks are simple... is there an entry in dnum[]?
    if (n < MINFS_DIRECT) {
        uint32_t bno;
        if ((bno = vn->inode.dnum[n]) == 0) {
            if (alloc) {
                block_t* blk = minfs_new_block(vn->fs, hint, &bno, bdata);
                if (blk != NULL) {
                    vn->inode.dnum[n] = bno;
                    vn->inode.block_count++;
                    minfs_sync_vnode(vn, MX_FS_SYNC_DEFAULT);
                }
                return blk;
            } else {
                return NULL;
            }
        }
        return bcache_get(vn->fs->bc, bno, bdata);
    }

    // for indirect blocks, adjust past the direct blocks
    n -= MINFS_DIRECT;

    // determine indices into the indirect block list and into
    // the block list in the indirect block
    uint32_t i = n / (MINFS_BLOCK_SIZE / sizeof(uint32_t));
    uint32_t j = n % (MINFS_BLOCK_SIZE / sizeof(uint32_t));

    if (i >= MINFS_INDIRECT) {
        return NULL;
    }

    uint32_t ibno;
    block_t* iblk;
    uint32_t* ientry;
    uint32_t iflags = 0;

    // look up the indirect bno
    if ((ibno = vn->inode.inum[i]) == 0) {
        if (!alloc) {
            return NULL;
        }
        // allocate a new indirect block
        if ((iblk = minfs_new_block(vn->fs, 0, &ibno, (void**) &ientry)) == NULL) {
            return NULL;
        }
        // record new indirect block in inode, note that we need to update
        vn->inode.block_count++;
        vn->inode.inum[i] = ibno;
        iflags = BLOCK_DIRTY;
    } else {
        if ((iblk = bcache_get(vn->fs->bc, ibno, (void**) &ientry)) == NULL) {
            error("minfs: cannot read indirect block @%u\n", ibno);
            return NULL;
        }
    }

    uint32_t bno;
    block_t* blk = NULL;
    if ((bno = ientry[j]) == 0) {
        if (alloc) {
            // allocate a new block
            blk = minfs_new_block(vn->fs, hint, &bno, bdata);
            if (blk != NULL) {
                vn->inode.block_count++;
                ientry[j] = bno;
                iflags = BLOCK_DIRTY;
            }
        }
    } else {
        blk = bcache_get(vn->fs->bc, bno, bdata);
    }

    // release indirect block, updating if necessary
    // and update the inode as well if we changed it
    bcache_put(vn->fs->bc, iblk, iflags);
    if (iflags & BLOCK_DIRTY) {
        minfs_sync_vnode(vn, MX_FS_SYNC_DEFAULT);
    }

    return blk;
}

static inline void vn_put_block(vnode_t* vn, block_t* blk) {
    bcache_put(vn->fs->bc, blk, 0);
}

static inline void vn_put_block_dirty(vnode_t* vn, block_t* blk) {
    bcache_put(vn->fs->bc, blk, BLOCK_DIRTY);
}

// Immediately stop iterating over the directory.
#define DIR_CB_DONE 0
// Access the next direntry in the directory. Offsets updated.
#define DIR_CB_NEXT 1
// Identify that the direntry record was modified. Stop iterating.
#define DIR_CB_SAVE_SYNC 2

static size_t _fs_read(vnode_t* vn, void* data, size_t len, size_t off);
static size_t _fs_write(vnode_t* vn, const void* data, size_t len, size_t off);
static mx_status_t _fs_truncate(vnode_t* vn, size_t len);

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
    uint32_t reclen = MINFS_RECLEN(de, off);
    if ((bytes_read < MINFS_DIRENT_SIZE) || (reclen < MINFS_DIRENT_SIZE)) {
        error("vn_dir: Could not read dirent at offset: %zd\n", off);
        return ERR_IO;
    } else if ((off + reclen > MINFS_MAX_DIRECTORY_SIZE) || (reclen & 3)) {
        error("vn_dir: bad reclen %u > %u\n", reclen, MINFS_MAX_DIRECTORY_SIZE);
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
    offs->off += MINFS_RECLEN(de, offs->off);
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
    if (vn->inode.magic == MINFS_MAGIC_DIR) {
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
    size_t off_next = off + MINFS_RECLEN(de, off);
    minfs_dirent_t de_prev, de_next;

    // Read the direntries we're considering merging with.
    // Verify they are free and small enough to merge.
    size_t coalesced_size = MINFS_RECLEN(de, off);
    // Coalesce with "next" first, so the MINFS_RECLEN_LAST bit can easily flow
    // back to "de" and "de_prev".
    if (!(de->reclen & MINFS_RECLEN_LAST)) {
        size_t r = _fs_read(vndir, &de_next, MINFS_DIRENT_SIZE, off_next);
        if (validate_dirent(&de_next, r, off_next) != NO_ERROR) {
            error("unlink: Failed to read next dirent\n");
            return ERR_IO;
        }
        if (de_next.ino == 0) {
            coalesced_size += MINFS_RECLEN(&de_next, off_next);
            // If the next entry *was* last, then 'de' is now last.
            de->reclen |= (de_next.reclen & MINFS_RECLEN_LAST);
        }
    }
    if (off_prev != off) {
        size_t r = _fs_read(vndir, &de_prev, MINFS_DIRENT_SIZE, off_prev);
        if (validate_dirent(&de_prev, r, off_prev) != NO_ERROR) {
            error("unlink: Failed to read previous dirent\n");
            return ERR_IO;
        }
        if (de_prev.ino == 0) {
            coalesced_size += MINFS_RECLEN(&de_prev, off_prev);
            off = off_prev;
        }
    }

    if (!(de->reclen & MINFS_RECLEN_LAST) && (coalesced_size >= MINFS_RECLEN_MASK)) {
        // Should only be possible if the on-disk record format is corrupted
        return ERR_IO;
    }
    de->ino = 0;
    de->reclen = (coalesced_size & MINFS_RECLEN_MASK) | (de->reclen & MINFS_RECLEN_LAST);
    size_t r = _fs_write(vndir, de, MINFS_DIRENT_SIZE, off);
    if (r != MINFS_DIRENT_SIZE) {
        error("unlink: Failed to updated directory\n");
        return ERR_IO;
    }

    if (de->reclen & MINFS_RECLEN_LAST) {
        // Truncating the directory merely removed unused space; if it fails,
        // the directory contents are still valid.
        _fs_truncate(vndir, off + MINFS_DIRENT_SIZE);
    }

    vn->inode.link_count--;
    vn_release(vn);

    // erase dirent (convert to empty entry), decrement dirent count
    vndir->inode.dirent_count--;
    minfs_sync_vnode(vndir, MX_FS_SYNC_MTIME);
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
    if ((status = minfs_vnode_get(vndir->fs, &vn, de->ino)) < 0) {
        return status;
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
    if ((status = minfs_vnode_get(vndir->fs, &vn, de->ino)) < 0) {
        return status;
    }
    return do_unlink(vndir, vn, de, offs);
}

// since these rename callbacks operates on a single name, they actually just do
// some validation and change an inode, rather than altering any names
static mx_status_t cb_dir_can_rename(vnode_t* vndir, minfs_dirent_t* de,
                                     dir_args_t* args, de_off_t* offs) {
    if ((de->ino == 0) || (args->len != de->namelen) ||
        memcmp(args->name, de->name, args->len)) {
        return do_next_dirent(de, offs);
    }

    vnode_t* vn;
    mx_status_t status;
    if ((status = minfs_vnode_get(vndir->fs, &vn, de->ino)) < 0) {
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

    vn_release(vn);
    return DIR_CB_DONE;
}

static mx_status_t cb_dir_update_inode(vnode_t* vndir, minfs_dirent_t* de,
                                       dir_args_t* args, de_off_t* offs) {
    if ((de->ino == 0) || (args->len != de->namelen) ||
        memcmp(args->name, de->name, args->len)) {
        return do_next_dirent(de, offs);
    }

    de->ino = args->ino;
    _fs_write(vndir, de, SIZEOF_MINFS_DIRENT(de->namelen), offs->off);
    return DIR_CB_SAVE_SYNC;
}

static mx_status_t fill_dirent(vnode_t* vndir, minfs_dirent_t* de,
                               dir_args_t* args, size_t off) {
    de->ino = args->ino;
    de->type = args->type;
    de->namelen = args->len;
    memcpy(de->name, args->name, args->len);
    vndir->inode.dirent_count++;
    _fs_write(vndir, de, SIZEOF_MINFS_DIRENT(de->namelen), off);
    return DIR_CB_SAVE_SYNC;
}

static mx_status_t cb_dir_append(vnode_t* vndir, minfs_dirent_t* de,
                                 dir_args_t* args, de_off_t* offs) {
    uint32_t reclen = MINFS_RECLEN(de, offs->off);
    if (de->ino == 0) {
        // empty entry, do we fit?
        if (args->reclen > reclen) {
            return do_next_dirent(de, offs);
        }
        return fill_dirent(vndir, de, args, offs->off);
    } else {
        // filled entry, can we sub-divide?
        uint32_t size = SIZEOF_MINFS_DIRENT(de->namelen);
        if (size > reclen) {
            error("bad reclen (smaller than dirent) %u < %u\n", reclen, size);
            return ERR_IO;
        }
        uint32_t extra = reclen - size;
        if (extra < args->reclen) {
            return do_next_dirent(de, offs);
        }
        // shrink existing entry
        bool was_last_record = de->reclen & MINFS_RECLEN_LAST;
        de->reclen = size;
        _fs_write(vndir, de, SIZEOF_MINFS_DIRENT(de->namelen), offs->off);
        offs->off += size;
        // create new entry in the remaining space
        de = ((void*)de) + size;
        char data[MINFS_MAX_DIRENT_SIZE];
        de = (minfs_dirent_t*) data;
        de->reclen = extra | (was_last_record ? MINFS_RECLEN_LAST : 0);
        return fill_dirent(vndir, de, args, offs->off);
    }
}

// Calls a callback 'func' on all direntries in a directory 'vn' with the
// provided arguments, reacting to the return code of the callback.
//
// When 'func' is called, it receives a few arguments:
//  'vndir': The directory on which the callback is operating
//  'de': A pointer the start of a single dirent.
//        Only SIZEOF_MINFS_DIRENT(de->namelen) bytes are guaranteed to exist in
//        memory from this starting pointer.
//  'args': Additional arguments plubmed through vn_dir_for_each
//  'offs': Offset info about where in the directory this direntry is located.
//          Since 'func' may create / remove surrounding dirents, it is responsible for
//          updating the offset information to access the next dirent.
static mx_status_t vn_dir_for_each(vnode_t* vn, dir_args_t* args,
                                   mx_status_t (*func)(vnode_t*, minfs_dirent_t*,
                                                       dir_args_t*, de_off_t* offs)) {
    char data[MINFS_MAX_DIRENT_SIZE];
    minfs_dirent_t* de = (minfs_dirent_t*) data;
    de_off_t offs = {
        .off = 0,
        .off_prev = 0,
    };
    mx_status_t status;
    while (offs.off + MINFS_DIRENT_SIZE < MINFS_MAX_DIRECTORY_SIZE) {
        trace(MINFS, "Reading dirent at offset %zd\n", offs.off);
        size_t r = _fs_read(vn, data, MINFS_MAX_DIRENT_SIZE, offs.off);
        if ((status = validate_dirent(de, r, offs.off)) != NO_ERROR) {
            return status;
        }

        switch ((status = func(vn, de, args, &offs))) {
        case DIR_CB_NEXT:
            break;
        case DIR_CB_SAVE_SYNC:
            vn->inode.seq_num++;
            minfs_sync_vnode(vn, MX_FS_SYNC_MTIME);
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
        list_delete(&vn->hashnode);
        free(vn);
    }
}

static mx_status_t fs_open(vnode_t** _vn, uint32_t flags) {
    vnode_t* vn = *_vn;
    trace(MINFS, "minfs_open() vn=%p(#%u)\n", vn, vn->ino);
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
    if (vn->inode.magic == MINFS_MAGIC_DIR) {
        return ERR_NOT_FILE;
    }
    return _fs_read(vn, data, len, off);
}

// Internal read. Usable on directories.
static size_t _fs_read(vnode_t* vn, void* data, size_t len, size_t off) {
    // clip to EOF
    if (off >= vn->inode.size) {
        return 0;
    }
    if (len > (vn->inode.size - off)) {
        len = vn->inode.size - off;
    }

    void* start = data;
    uint32_t n = off / MINFS_BLOCK_SIZE;
    size_t adjust = off % MINFS_BLOCK_SIZE;

    while ((len > 0) && (n < MINFS_MAX_FILE_BLOCK)) {
        size_t xfer;
        if (len > (MINFS_BLOCK_SIZE - adjust)) {
            xfer = MINFS_BLOCK_SIZE - adjust;
        } else {
            xfer = len;
        }

        block_t* blk;
        void* bdata;
        if ((blk = vn_get_block(vn, n, &bdata, false)) != NULL) {
            memcpy(data, bdata + adjust, xfer);
            vn_put_block(vn, blk);
        } else {
            // If the block is not allocated, just read zeros
            memset(data, 0, xfer);
        }

        adjust = 0;
        len -= xfer;
        data += xfer;
        n++;
    }
    return data - start;
}

static ssize_t fs_write(vnode_t* vn, const void* data, size_t len, size_t off) {
    trace(MINFS, "minfs_write() vn=%p(#%u) len=%zd off=%zd\n", vn, vn->ino, len, off);
    if (vn->inode.magic == MINFS_MAGIC_DIR) {
        return ERR_NOT_FILE;
    }
    return _fs_write(vn, data, len, off);
}

// Internal write. Usable on directories.
static size_t _fs_write(vnode_t* vn, const void* data, size_t len, size_t off) {
    if (len == 0) {
        return 0;
    }

    const void* start = data;
    uint32_t n = off / MINFS_BLOCK_SIZE;
    size_t adjust = off % MINFS_BLOCK_SIZE;

    while ((len > 0) && (n < MINFS_MAX_FILE_BLOCK)) {
        size_t xfer;
        if (len > (MINFS_BLOCK_SIZE - adjust)) {
            xfer = MINFS_BLOCK_SIZE - adjust;
        } else {
            xfer = len;
        }

        block_t* blk;
        void* bdata;
        if ((blk = vn_get_block(vn, n, &bdata, true)) == NULL) {
            break;
        }
        memcpy(bdata + adjust, data, xfer);
        vn_put_block_dirty(vn, blk);

        adjust = 0;
        len -= xfer;
        data += xfer;
        n++;
    }

    len = data - start;
    if (len == 0) {
        // If more than zero bytes were requested, but zero bytes were written,
        // return an error explicitly (rather than zero).
        return ERR_NO_RESOURCES;
    }
    if ((off + len) > vn->inode.size) {
        vn->inode.size = off + len;
    }

    minfs_sync_vnode(vn, MX_FS_SYNC_MTIME);  // writes always update mtime
    return len;
}

static mx_status_t fs_lookup(vnode_t* vn, vnode_t** out, const char* name, size_t len) {
    trace(MINFS, "minfs_lookup() vn=%p(#%u) name='%.*s'\n", vn, vn->ino, (int)len, name);
    if (vn->inode.magic != MINFS_MAGIC_DIR) {
        error("not directory\n");
        return ERR_NOT_SUPPORTED;
    }
    dir_args_t args = {
        .name = name,
        .len = len,
    };
    mx_status_t status;
    if ((status = vn_dir_for_each(vn, &args, cb_dir_find)) < 0) {
        return status;
    }
    if ((status = minfs_vnode_get(vn->fs, &vn, args.ino)) < 0) {
        return status;
    }
    *out = vn;
    return NO_ERROR;
}

static mx_status_t fs_getattr(vnode_t* vn, vnattr_t* a) {
    trace(MINFS, "minfs_getattr() vn=%p(#%u)\n", vn, vn->ino);
    a->inode = vn->ino;
    a->size = vn->inode.size;
    a->mode = DTYPE_TO_VTYPE(MINFS_MAGIC_TYPE(vn->inode.magic));
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
        minfs_sync_vnode(vn, MX_FS_SYNC_DEFAULT);
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
    dircookie_t* dc = cookie;
    vdirent_t* out = dirents;

    if (vn->inode.magic != MINFS_MAGIC_DIR) {
        return ERR_NOT_SUPPORTED;
    }

    size_t off;
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

    char data[MINFS_MAX_DIRENT_SIZE];
    minfs_dirent_t* de = (minfs_dirent_t*) data;
    size_t r;
    while (off + MINFS_DIRENT_SIZE < MINFS_MAX_DIRECTORY_SIZE) {
        r = _fs_read(vn, de, MINFS_MAX_DIRENT_SIZE, off);
        if (validate_dirent(de, r, off) != NO_ERROR) {
            goto fail;
        }

        if (de->ino) {
            mx_status_t status;
            size_t len_remaining = len - (size_t)((void*)out - dirents);
            if ((status = vfs_fill_dirent(out, len_remaining, de->name,
                                          de->namelen, de->type)) < 0) {
                // no more space
                goto done;
            }
            out = ((void*) out) + status;
        }

        off += MINFS_RECLEN(de, off);
    }

done:
    // save our place in the dircookie
    dc->flags |= DIRCOOKIE_FLAG_USED;
    dc->off = off;
    dc->seqno = vn->inode.seq_num;
    r = ((void*) out) - dirents;
    assert(r <= len); // Otherwise, we're overflowing the input buffer.
    return r;

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
    if (vndir->inode.magic != MINFS_MAGIC_DIR) {
        return ERR_NOT_SUPPORTED;
    } else if (len > MINFS_MAX_NAME_SIZE) {
        return ERR_NOT_SUPPORTED;
    }

    dir_args_t args = {
        .name = name,
        .len = len,
    };
    // ensure file does not exist
    mx_status_t status;
    if ((status = vn_dir_for_each(vndir, &args, cb_dir_find)) != ERR_NOT_FOUND) {
        return ERR_ALREADY_EXISTS;
    }

    // creating a directory?
    uint32_t type = S_ISDIR(mode) ? MINFS_TYPE_DIR : MINFS_TYPE_FILE;

    // mint a new inode and vnode for it
    vnode_t* vn;
    if ((status = minfs_vnode_new(vndir->fs, &vn, type)) < 0) {
        return status;
    }

    // add directory entry for the new child node
    args.ino = vn->ino;
    args.type = type;
    args.reclen = SIZEOF_MINFS_DIRENT(len);
    if ((status = vn_dir_for_each(vndir, &args, cb_dir_append)) < 0) {
        return status;
    }

    if (type == MINFS_TYPE_DIR) {
        void* bdata;
        block_t* blk;
        if ((blk = minfs_new_block(vndir->fs, 0, vn->inode.dnum + 0, &bdata)) == NULL) {
            panic("failed to create directory");
        }
        minfs_dir_init(bdata, vn->ino, vndir->ino);
        bcache_put(vndir->fs->bc, blk, BLOCK_DIRTY);
        vn->inode.block_count = 1;
        vn->inode.dirent_count = 2;
        vn->inode.size = MINFS_BLOCK_SIZE;
        minfs_sync_vnode(vn, MX_FS_SYNC_DEFAULT);
    }
    *out = vn;
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
            return minfs_unmount(vn->fs);
        }
        default: {
            return ERR_NOT_SUPPORTED;
        }
    }
}

static mx_status_t fs_unlink(vnode_t* vn, const char* name, size_t len) {
    trace(MINFS, "minfs_unlink() vn=%p(#%u) name='%.*s'\n", vn, vn->ino, (int)len, name);
    if (vn->inode.magic != MINFS_MAGIC_DIR) {
        return ERR_NOT_SUPPORTED;
    }
    if ((len == 1) && (name[0] == '.')) {
        return ERR_BAD_STATE;
    }
    if ((len == 2) && (name[0] == '.') && (name[1] == '.')) {
        return ERR_BAD_STATE;
    }
    dir_args_t args = {
        .name = name,
        .len = len,
    };
    return vn_dir_for_each(vn, &args, cb_dir_unlink);
}

static mx_status_t fs_truncate(vnode_t* vn, size_t len) {
    if (vn->inode.magic == MINFS_MAGIC_DIR) {
        return ERR_NOT_FILE;
    }

    return _fs_truncate(vn, len);
}

static mx_status_t _fs_truncate(vnode_t* vn, size_t len) {
    mx_status_t r = 0;
    if (len < vn->inode.size) {
        // Truncate should make the file shorter
        size_t bno = vn->inode.size / MINFS_BLOCK_SIZE;
        size_t trunc_bno = len / MINFS_BLOCK_SIZE;

        // Truncate to the nearest block
        if (trunc_bno <= bno) {
            size_t start_bno = (len % MINFS_BLOCK_SIZE == 0) ? trunc_bno : trunc_bno + 1;
            if ((r = vn_blocks_shrink(vn, start_bno)) < 0) {
                return r;
            }

            if (start_bno * MINFS_BLOCK_SIZE < vn->inode.size) {
                vn->inode.size = start_bno * MINFS_BLOCK_SIZE;
            }
        }

        // Write zeroes to the rest of the remaining block, if it exists
        if (len < vn->inode.size) {
            void* bdata;
            block_t* blk;
            size_t adjust = len % MINFS_BLOCK_SIZE;
            if ((blk = vn_get_block(vn, len / MINFS_BLOCK_SIZE, &bdata, false)) != NULL) {
                memset(bdata + adjust, 0, MINFS_BLOCK_SIZE - adjust);
                vn_put_block_dirty(vn, blk);
            }
        }
        vn->inode.size = len;
        minfs_sync_vnode(vn, MX_FS_SYNC_MTIME);
    } else if (len > vn->inode.size) {
        // Truncate should make the file longer, filled with zeroes.
        if (MINFS_MAX_FILE_SIZE < len) {
            return ERR_INVALID_ARGS;
        }
        char zero = 0;
        if ((r = fs_write(vn, &zero, 1, len - 1)) < 0) {
            return r;
        }
    }
    return NO_ERROR;
}

// verify that the 'newdir' inode is not a subdirectory of the source.
static mx_status_t check_not_subdirectory(vnode_t* src, vnode_t* newdir) {
    vnode_t* vn = newdir;
    vnode_t* out = NULL;
    mx_status_t status = NO_ERROR;
    // Acquire vn here so this function remains cleanly idempotent with respect
    // to refcounts. 'newdir' and all ancestors (until an exit condition is
    // reached) will be acquired once and released once.
    vn_acquire(vn);
    while (vn->ino != MINFS_ROOT_INO) {
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
                             const char* newname, size_t newlen) {
    trace(MINFS, "minfs_rename() olddir=%p(#%u) newdir=%p(#%u) oldname='%.*s' newname='%.*s'\n",
          olddir, olddir->ino, newdir, newdir->ino, (int)oldlen, oldname, (int)newlen, newname);

    // ensure that the vnodes containin oldname and newname are directories
    if (olddir->inode.magic != MINFS_MAGIC_DIR || newdir->inode.magic != MINFS_MAGIC_DIR)
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
    vnode_t* oldvn = NULL;
    // acquire the 'oldname' node (it must exist)
    dir_args_t args = {
        .name = oldname,
        .len = oldlen,
    };
    if ((status = vn_dir_for_each(olddir, &args, cb_dir_find)) < 0) {
        return status;
    } else if ((status = minfs_vnode_get(olddir->fs, &oldvn, args.ino)) < 0) {
        return status;
    } else if ((status = check_not_subdirectory(oldvn, newdir)) < 0) {
        goto done;
    }

    // if the entry for 'newname' exists, make sure it can be replaced by
    // the vnode behind 'oldname'.
    args.name = newname;
    args.len = newlen;
    args.ino = oldvn->ino;
    args.type = (oldvn->inode.magic == MINFS_MAGIC_DIR) ? MINFS_TYPE_DIR : MINFS_TYPE_FILE;
    status = vn_dir_for_each(newdir, &args, cb_dir_can_rename);
    if (status == ERR_NOT_FOUND) {
        // if 'newname' does not exist, create it
        args.reclen = SIZEOF_MINFS_DIRENT(newlen);
        if ((status = vn_dir_for_each(newdir, &args, cb_dir_append)) < 0) {
            goto done;
        }
        status = 0;
    } else if (status == 0) {
        // if 'newname' does exist, replace its inode.
        status = vn_dir_for_each(newdir, &args, cb_dir_update_inode);
    }

    if (status != 0) {
        goto done;
    }

    // update the oldvn's entry for '..' if (1) it was a directory, and (2) it
    // moved to a new directory
    if ((args.type == MINFS_TYPE_DIR) && (olddir->ino != newdir->ino)) {
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
    return bcache_sync(vn->fs->bc);
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

