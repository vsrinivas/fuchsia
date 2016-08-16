// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include <mxio/vfs.h>

#include "minfs-private.h"


// obtain the nth block of a vnode
static block_t* vn_get_block(minfs_vnode_t* vn, uint32_t n, void** bdata) {
    if (n < MINFS_DIRECT) {
        n = vn->inode.dnum[n];
        if (n != 0) {
            return bcache_get(vn->fs->bc, n, bdata);
        }
    }
    //TODO: indirect blocks
    return NULL;
}

static inline void vn_put_block(minfs_vnode_t* vn, block_t* blk) {
    bcache_put(vn->fs->bc, blk, 0);
}

static inline void vn_put_block_dirty(minfs_vnode_t* vn, block_t* blk) {
    bcache_put(vn->fs->bc, blk, BLOCK_DIRTY);
}

block_t* minfs_new_block(minfs_t* fs, uint32_t hint, uint32_t* out_bno, void** bdata) {
    uint32_t bno = bitmap_alloc(&fs->block_map, hint);
    if ((bno == BITMAP_FAIL) && (hint != 0)) {
        bno = bitmap_alloc(&fs->block_map, 0);
    }
    if (bno == BITMAP_FAIL) {
        return NULL;
    }

    // obtain the block of the alloc bitmap we need
    block_t* block_abm;
    void* bdata_abm;
    if ((block_abm = bcache_get(fs->bc, fs->info.abm_block + (bno / MINFS_BLOCK_BITS), &bdata_abm)) == NULL) {
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
    memcpy(bdata_abm, fs->block_map.map + ((bno / MINFS_BLOCK_BITS) * (MINFS_BLOCK_BITS / 64)), MINFS_BLOCK_SIZE);
    bcache_put(fs->bc, block_abm, BLOCK_DIRTY);
    *out_bno = bno;
    return block;
}



#define DIR_CB_NEXT 0
#define DIR_CB_DONE 1
#define DIR_CB_SAVE 2

typedef struct dir_args {
    const char* name;
    size_t len;
    uint32_t ino;
    uint32_t type;
    uint32_t reclen;
} dir_args_t;

static uint32_t cb_dir_find(minfs_dirent_t* de, dir_args_t* args) {
    if (de->ino == 0) {
        return DIR_CB_NEXT;
    }
    if ((de->namelen == args->len) && (!memcmp(de->name, args->name, args->len))) {
        args->ino = de->ino;
        args->type = de->type;
        return DIR_CB_DONE;
    }
    return DIR_CB_NEXT;
}

static uint32_t cb_dir_append(minfs_dirent_t* de, dir_args_t* args) {
    if (de->ino == 0) {
        // empty entry, do we fit?
        if (args->reclen > de->reclen) {
            return DIR_CB_NEXT;
        }
    } else {
        // filled entry, can we sub-divide?
        uint32_t size = SIZEOF_MINFS_DIRENT(de->namelen);
        if (size > de->reclen) {
            error("bad reclen %u < %u\n", de->reclen, size);
            return DIR_CB_DONE;
        }
        uint32_t extra = de->reclen - size;
        if (extra < args->reclen) {
            return DIR_CB_NEXT;
        }
        // shrink existing entry
        de->reclen = size;
        // create new entry in the remaining space
        de = ((void*)de) + size;
        de->reclen = extra;
    }
    de->ino = args->ino;
    de->type = args->type;
    de->namelen = args->len;
    memcpy(de->name, args->name, args->len);
    return DIR_CB_SAVE;
}

static mx_status_t vn_dir_for_each(minfs_vnode_t* vn, dir_args_t* args,
                                   uint32_t (*func)(minfs_dirent_t* de, dir_args_t* args)) {
    for (unsigned n = 0; n < vn->inode.block_count; n++) {
        block_t* blk;
        void* data;
        if ((blk = vn_get_block(vn, n, &data)) == NULL) {
            error("vn_dir: vn=%p missing block %u\n", vn, n);
            return ERR_NOT_FOUND;
        }
        uint32_t size = MINFS_BLOCK_SIZE;
        minfs_dirent_t* de = data;
        while (size > MINFS_DIRENT_SIZE) {
            //fprintf(stderr,"DE ino=%u rlen=%u nlen=%u\n", de->ino, de->reclen, de->namelen);
            uint32_t rlen = de->reclen;
            if ((rlen > size) || (rlen & 3)) {
                error("vn_dir: vn=%p bad reclen %u > %u\n", vn, rlen, size);
                break;
            }
            if (de->ino != 0) {
                if ((de->namelen == 0) || (de->namelen > (rlen - MINFS_DIRENT_SIZE))) {
                    error("vn_dir: vn=%p bad namelen %u / %u\n", vn, de->namelen, rlen);
                    break;
                }
            }
            switch (func(de, args)) {
            case DIR_CB_DONE:
                vn_put_block(vn, blk);
                return NO_ERROR;
            case DIR_CB_SAVE:
                vn_put_block_dirty(vn, blk);
                return NO_ERROR;
            default:
                break;
            }
            de = ((void*) de) + rlen;
            size -= rlen;
        }
        vn_put_block(vn, blk);
    }
    return ERR_NOT_FOUND;
}

static void fs_release(vnode_t* _vn) {
    minfs_vnode_t* vn = to_minvn(_vn);
    trace(MINFS, "minfs_release() vn=%p(#%u)\n", vn, vn->ino);
}

static mx_status_t fs_open(vnode_t** _vn, uint32_t flags) {
    minfs_vnode_t* vn = to_minvn(*_vn);
    trace(MINFS, "minfs_open() vn=%p(#%u)\n", vn, vn->ino);
    return NO_ERROR;
}

static mx_status_t fs_close(vnode_t* _vn) {
    minfs_vnode_t* vn = to_minvn(_vn);
    trace(MINFS, "minfs_close() vn=%p(#%u)\n", vn, vn->ino);
    return NO_ERROR;
}

static ssize_t fs_read(vnode_t* _vn, void* data, size_t len, size_t off) {
    minfs_vnode_t* vn = to_minvn(_vn);
    trace(MINFS, "minfs_read() vn=%p(#%u) len=%zd off=%zd\n", vn, vn->ino, len, off);
    return ERR_NOT_SUPPORTED;
}

static ssize_t fs_write(vnode_t* _vn, const void* data, size_t len, size_t off) {
    minfs_vnode_t* vn = to_minvn(_vn);
    trace(MINFS, "minfs_write() vn=%p(#%u) len=%zd off=%zd\n", vn, vn->ino, len, off);
    return ERR_NOT_SUPPORTED;
}

static mx_status_t fs_lookup(vnode_t* _vn, vnode_t** out, const char* name, size_t len) {
    minfs_vnode_t* vn = to_minvn(_vn);
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
    if ((status = minfs_get_vnode(vn->fs, &vn, args.ino)) < 0) {
        return status;
    }
    *out = &vn->vnode;
    return NO_ERROR;
}

static mx_status_t fs_getattr(vnode_t* _vn, vnattr_t* a) {
    minfs_vnode_t* vn = to_minvn(_vn);
    trace(MINFS, "minfs_getattr() vn=%p(#%u)\n", vn, vn->ino);
    return ERR_NOT_SUPPORTED;
}

static mx_status_t fs_readdir(vnode_t* _vn, void* cookie, void* dirents, size_t len) {
    minfs_vnode_t* vn = to_minvn(_vn);
    trace(MINFS, "minfs_readdir() vn=%p(#%u) cookie=%p len=%zd\n", vn, vn->ino, cookie, len);
    if (vn->inode.magic != MINFS_MAGIC_DIR) {
        return ERR_NOT_SUPPORTED;
    }
    return ERR_NOT_SUPPORTED;
}

static mx_status_t fs_create(vnode_t* _vn, vnode_t** out,
                             const char* name, size_t len, uint32_t mode) {
    minfs_vnode_t* vndir = to_minvn(_vn);
    trace(MINFS, "minfs_create() vn=%p(#%u) name='%.*s' mode=%#x\n",
          vndir, vndir->ino, (int)len, name, mode);
    if (vndir->inode.magic != MINFS_MAGIC_DIR) {
        return ERR_NOT_SUPPORTED;
    }
    dir_args_t args = {
        .name = name,
        .len = len,
    };
    // ensure file does not exist
    mx_status_t status;
    if ((status = vn_dir_for_each(vndir, &args, cb_dir_find)) != ERR_NOT_FOUND) {
        return ERR_IO; //TODO: err exists
    }

    // creating a directory?
    uint32_t type = (mode & 0x80000000) ? MINFS_TYPE_DIR : MINFS_TYPE_FILE;

    // mint a new inode and vnode for it
    minfs_vnode_t* vn;
    if ((status = minfs_new_vnode(vndir->fs, &vn, type)) < 0) {
        return status;
    }

    // add directory entry for the new child node
    args.ino = vn->ino;
    args.type = type;
    args.reclen = SIZEOF_MINFS_DIRENT(len);
    if ((status = vn_dir_for_each(vndir, &args, cb_dir_append)) < 0) {
        error("minfs_create() dir append failed %d\n", status);
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
        vn->inode.size = MINFS_BLOCK_SIZE;
        minfs_sync_vnode(vn);
    }
    *out = &vn->vnode;
    return NO_ERROR;
}

static ssize_t fs_ioctl(vnode_t* vn, uint32_t op, const void* in_buf,
                            size_t in_len, void* out_buf, size_t out_len) {
    return ERR_NOT_SUPPORTED;
}

static mx_status_t fs_unlink(vnode_t* _vn, const char* name, size_t len) {
    minfs_vnode_t* vn = to_minvn(_vn);
    trace(MINFS, "minfs_unlink() vn=%p(#%u) name='%.*s'\n", vn, vn->ino, (int)len, name);
    if (vn->inode.magic != MINFS_MAGIC_DIR) {
        return ERR_NOT_SUPPORTED;
    }
    return ERR_NOT_SUPPORTED;
}

vnode_ops_t minfs_ops = {
    .release = fs_release,
    .open = fs_open,
    .close = fs_close,
    .read = fs_read,
    .write = fs_write,
    .lookup = fs_lookup,
    .getattr = fs_getattr,
    .readdir = fs_readdir,
    .create = fs_create,
    .ioctl = fs_ioctl,
    .unlink = fs_unlink,
};

