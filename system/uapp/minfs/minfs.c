// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "minfs-private.h"

void minfs_dump_info(minfs_info_t* info) {
    printf("minfs: blocks:  %10u (size %u)\n", info->block_count, info->block_size);
    printf("minfs: inodes:  %10u (size %u)\n", info->inode_count, info->inode_size);
    printf("minfs: inode bitmap @ %10u\n", info->ibm_block);
    printf("minfs: alloc bitmap @ %10u\n", info->abm_block);
    printf("minfs: inode table  @ %10u\n", info->ino_block);
    printf("minfs: data blocks  @ %10u\n", info->dat_block);
}

mx_status_t minfs_check_info(minfs_info_t* info, uint32_t max) {
    if ((info->magic0 != MINFS_MAGIC0) ||
        (info->magic1 != MINFS_MAGIC1)) {
        error("minfs: bad magic\n");
        return ERR_INVALID_ARGS;
    }
    if (info->version != MINFS_VERSION) {
        error("minfs: bad version %08x\n", info->version);
        return ERR_INVALID_ARGS;
    }
    if ((info->block_size != MINFS_BLOCK_SIZE) ||
        (info->inode_size != MINFS_INODE_SIZE)) {
        error("minfs: bsz/isz %u/%u unsupported\n", info->block_size, info->inode_size);
        return ERR_INVALID_ARGS;
    }
    if (info->block_count > max) {
        error("minfs: too large for device\n");
        return ERR_INVALID_ARGS;
    }
    //TODO: validate layout
    return 0;
}

static uint64_t minfs_current_utc_time(void) {
    // placeholder to provide changing values for file time (in unix epoch time)
    // TODO(orr) replace with syscall when RTC info is available
    static uint64_t timestamp = 0xdeadbeef;

    return timestamp++;
}

void minfs_sync_vnode(vnode_t* vn) {
    block_t* blk;
    void* bdata;

    uint32_t bno_of_ino = vn->fs->info.ino_block + (vn->ino / MINFS_INODES_PER_BLOCK);
    uint32_t off_of_ino = (vn->ino % MINFS_INODES_PER_BLOCK) * MINFS_INODE_SIZE;

    if ((blk = bcache_get(vn->fs->bc, bno_of_ino, &bdata)) == NULL) {
        panic("failed sync vnode %p(#%u)", vn, vn->ino);
    }

    memcpy(bdata + off_of_ino, &vn->inode, MINFS_INODE_SIZE);
    bcache_put(vn->fs->bc, blk, BLOCK_DIRTY);

    // update modify time
    vn->inode.modify_time = minfs_current_utc_time();
}

mx_status_t minfs_ino_free(minfs_t* fs, uint32_t ino) {
    // locate data and block offset of bitmap
    void *bmdata;
    uint32_t bmbno;
    if ((bmdata = minfs_bitmap_block(&fs->inode_map, &bmbno, ino)) == NULL) {
        panic("inode not in bitmap");
    }

    // obtain the block of the inode bitmap we need
    block_t* block_ibm;
    void* bdata_ibm;
    if ((block_ibm = bcache_get(fs->bc, fs->info.ibm_block + bmbno, &bdata_ibm)) == NULL) {
        return ERR_IO;
    }

    // update and commit block to disk
    bitmap_clr(&fs->inode_map, ino);
    memcpy(bdata_ibm, bmdata, MINFS_BLOCK_SIZE);
    bcache_put(fs->bc, block_ibm, BLOCK_DIRTY);

    return NO_ERROR;
}

mx_status_t minfs_ino_alloc(minfs_t* fs, minfs_inode_t* inode, uint32_t* ino_out) {
    uint32_t ino = bitmap_alloc(&fs->inode_map, 0);
    if (ino == BITMAP_FAIL) {
        return ERR_NO_RESOURCES;
    }

    // locate data and block offset of bitmap
    void *bmdata;
    uint32_t bmbno;
    if ((bmdata = minfs_bitmap_block(&fs->inode_map, &bmbno, ino)) == NULL) {
        panic("inode not in bitmap");
    }

    // obtain the block of the inode bitmap we need
    block_t* block_ibm;
    void* bdata_ibm;
    if ((block_ibm = bcache_get(fs->bc, fs->info.ibm_block + bmbno, &bdata_ibm)) == NULL) {
        bitmap_clr(&fs->inode_map, ino);
        return ERR_IO;
    }

    uint32_t bno_of_ino = fs->info.ino_block + (ino / MINFS_INODES_PER_BLOCK);
    uint32_t off_of_ino = (ino % MINFS_INODES_PER_BLOCK) * MINFS_INODE_SIZE;

    // obtain the block of the inode table we need
    block_t* block_ino;
    void* bdata_ino;
    if ((block_ino = bcache_get(fs->bc, bno_of_ino, &bdata_ino)) == NULL) {
        bitmap_clr(&fs->inode_map, ino);
        bcache_put(fs->bc, block_ibm, 0);
        return ERR_IO;
    }

    //TODO: optional sanity check of both blocks

    // write data to blocks in memory
    memcpy(bdata_ibm, bmdata, MINFS_BLOCK_SIZE);
    memcpy(bdata_ino + off_of_ino, inode, MINFS_INODE_SIZE);

    // commit blocks to disk
    bcache_put(fs->bc, block_ibm, BLOCK_DIRTY);
    bcache_put(fs->bc, block_ino, BLOCK_DIRTY);

    *ino_out = ino;
    return NO_ERROR;
}

mx_status_t minfs_vnode_new(minfs_t* fs, vnode_t** out, uint32_t type) {
    vnode_t* vn;
    if ((type != MINFS_TYPE_FILE) && (type != MINFS_TYPE_DIR)) {
        return ERR_INVALID_ARGS;
    }
    if ((vn = calloc(1, sizeof(vnode_t))) == NULL) {
        return ERR_NO_MEMORY;
    }
    vn->inode.magic = MINFS_MAGIC(type);
    // TODO(orr) update when mx_time_get() works with unix epoch time
    vn->inode.create_time = vn->inode.modify_time = minfs_current_utc_time();
    vn->inode.link_count = 1;
    vn->refcount = 1;
    vn->ops = &minfs_ops;
    if (minfs_ino_alloc(fs, &vn->inode, &vn->ino) < 0) {
        free(vn);
        return ERR_NO_RESOURCES;
    }
    vn->fs = fs;
    list_add_tail(fs->vnode_hash + INO_HASH(vn->ino), &vn->hashnode);

    trace(MINFS, "new_vnode() %p(#%u) { magic=%#08x }\n",
          vn, vn->ino, vn->inode.magic);

    *out = vn;
    return 0;
}

mx_status_t minfs_vnode_get(minfs_t* fs, vnode_t** out, uint32_t ino) {
    if ((ino < 1) || (ino >= fs->info.inode_count)) {
        return ERR_OUT_OF_RANGE;
    }
    vnode_t* vn;
    uint32_t bucket = INO_HASH(ino);
    list_for_every_entry(fs->vnode_hash + bucket, vn, vnode_t, hashnode) {
        if (vn->ino == ino) {
            vn_acquire(vn);
            *out = vn;
            return NO_ERROR;
        }
    }
    if ((vn = calloc(1, sizeof(vnode_t))) == NULL) {
        return ERR_NO_MEMORY;
    }
    mx_status_t status;
    uint32_t ino_per_blk = fs->info.block_size / MINFS_INODE_SIZE;
    if ((status = bcache_read(fs->bc, fs->info.ino_block + ino / ino_per_blk, &vn->inode,
                              MINFS_INODE_SIZE * (ino % ino_per_blk), MINFS_INODE_SIZE)) < 0) {
        return status;
    }
    trace(MINFS, "get_vnode() %p(#%u) { magic=%#08x size=%u blks=%u dn=%u,%u,%u,%u... }\n",
          vn, ino, vn->inode.magic, vn->inode.size, vn->inode.block_count,
          vn->inode.dnum[0], vn->inode.dnum[1], vn->inode.dnum[2],
          vn->inode.dnum[3]);
    vn->fs = fs;
    vn->ino = ino;
    vn->refcount = 1;
    vn->ops = &minfs_ops;
    list_add_tail(fs->vnode_hash + bucket, &vn->hashnode);

    *out = vn;
    return NO_ERROR;
}

void minfs_dir_init(void* bdata, uint32_t ino_self, uint32_t ino_parent) {
#define DE0_SIZE SIZEOF_MINFS_DIRENT(1)

    // directory entry for self
    minfs_dirent_t* de = (void*) bdata;
    de->ino = ino_self;
    de->reclen = DE0_SIZE;
    de->namelen = 1;
    de->type = MINFS_TYPE_DIR;
    de->name[0] = '.';

    // directory entry for parent
    de = (void*) bdata + DE0_SIZE;
    de->ino = ino_parent;
    de->reclen = MINFS_BLOCK_SIZE - DE0_SIZE;
    de->namelen = 2;
    de->type = MINFS_TYPE_DIR;
    de->name[0] = '.';
    de->name[1] = '.';
}

mx_status_t minfs_create(minfs_t** out, bcache_t* bc, minfs_info_t* info) {
    uint32_t blocks = bcache_max_block(bc);
    uint32_t inodes = info->inode_count;

    mx_status_t status = minfs_check_info(info, blocks);
    if (status < 0) {
        return status;
    }

    minfs_t* fs = calloc(1, sizeof(minfs_t));
    if (fs == NULL) {
        return ERR_NO_MEMORY;
    }
    for (int n = 0; n < MINFS_BUCKETS; n++) {
        list_initialize(fs->vnode_hash + n);
    }
    memcpy(&fs->info, info, sizeof(minfs_info_t));
    fs->bc = bc;

    // determine how many blocks of inodes, allocation bitmaps,
    // and inode bitmaps there are
    //uint32_t inoblks = (inodes + MINFS_INODES_PER_BLOCK - 1) / MINFS_INODES_PER_BLOCK;
    fs->abmblks = (blocks + MINFS_BLOCK_BITS - 1) / MINFS_BLOCK_BITS;
    fs->ibmblks = (inodes + MINFS_BLOCK_BITS - 1) / MINFS_BLOCK_BITS;

    if ((status = bitmap_init(&fs->block_map, fs->abmblks * MINFS_BLOCK_BITS)) < 0) {
        free(fs);
        return status;
    }
    if ((status = bitmap_init(&fs->inode_map, fs->ibmblks * MINFS_BLOCK_BITS)) < 0) {
        bitmap_destroy(&fs->block_map);
        free(fs);
        return status;
    }
    // this keeps the underlying storage a block multiple but ensures we
    // can't allocate beyond the last real block or inode
    bitmap_resize(&fs->block_map, fs->info.block_count);
    bitmap_resize(&fs->inode_map, fs->info.inode_count);

    *out = fs;
    return NO_ERROR;
}

void minfs_destroy(minfs_t* fs) {
}

mx_status_t minfs_load_bitmaps(minfs_t* fs) {
    for (uint32_t n = 0; n < fs->abmblks; n++) {
        void* bmdata = minfs_bitmap_nth_block(&fs->block_map, n);
        if (bcache_read(fs->bc, fs->info.abm_block + n, bmdata, 0, MINFS_BLOCK_SIZE)) {
            error("minfs: failed reading alloc bitmap\n");
        }
    }
    for (uint32_t n = 0; n < fs->ibmblks; n++) {
        void* bmdata = minfs_bitmap_nth_block(&fs->inode_map, n);
        if (bcache_read(fs->bc, fs->info.ibm_block + n, bmdata, 0, MINFS_BLOCK_SIZE)) {
            error("minfs: failed reading inode bitmap\n");
        }
    }
    return NO_ERROR;
}

mx_status_t minfs_mount(vnode_t** out, bcache_t* bc) {
    minfs_info_t info;

    if (bcache_read(bc, 0, &info, 0, sizeof(info)) < 0) {
        error("minfs: could not read info block\n");
        return -1;
    }
    if (minfs_check_info(&info, bcache_max_block(bc))) {
        return -1;
    }

    minfs_t* fs;
    if (minfs_create(&fs, bc, &info)) {
        error("minfs: mount failed\n");
        return -1;
    }
    if (minfs_load_bitmaps(fs)) {
        return -1;
    }

    vnode_t* vn;
    if (minfs_vnode_get(fs, &vn, MINFS_ROOT_INO)) {
        error("minfs: cannot find root inode\n");
        return -1;
    }

    *out = vn;
    return NO_ERROR;
}

int minfs_mkfs(bcache_t* bc) {
    uint32_t blocks = bcache_max_block(bc);
    uint32_t inodes = 32768;

    // determine how many blocks of inodes, allocation bitmaps,
    // and inode bitmaps there are
    uint32_t inoblks = (inodes + MINFS_INODES_PER_BLOCK - 1) / MINFS_INODES_PER_BLOCK;
    uint32_t abmblks = (blocks + MINFS_BLOCK_BITS - 1) / MINFS_BLOCK_BITS;
    uint32_t ibmblks = (inodes + MINFS_BLOCK_BITS - 1) / MINFS_BLOCK_BITS;

    minfs_info_t info;
    memset(&info, 0x00, sizeof(info));
    info.magic0 = MINFS_MAGIC0;
    info.magic1 = MINFS_MAGIC1;
    info.version = MINFS_VERSION;
    info.flags = MINFS_FLAG_CLEAN;
    info.block_size = MINFS_BLOCK_SIZE;
    info.inode_size = MINFS_INODE_SIZE;
    info.block_count = blocks;
    info.inode_count = inodes;
    info.ibm_block = 8;
    info.abm_block = 16;
    info.ino_block = info.abm_block + ((abmblks + 8) & (~7));
    info.dat_block = info.ino_block + inoblks;
    minfs_dump_info(&info);

    bitmap_t abm;
    bitmap_t ibm;
    if (bitmap_init(&abm, info.block_count)) {
        return -1;
    }
    if (bitmap_init(&ibm, info.inode_count)) {
        bitmap_destroy(&abm);
        return -1;
    }

    void* bdata;
    block_t* blk;

    // write rootdir
    blk = bcache_get_zero(bc, info.dat_block, &bdata);
    minfs_dir_init(bdata, MINFS_ROOT_INO, MINFS_ROOT_INO);
    bcache_put(bc, blk, BLOCK_DIRTY);

    // update inode bitmap
    bitmap_set(&ibm, 0);
    bitmap_set(&ibm, MINFS_ROOT_INO);

    // update block bitmap:
    // reserve all blocks before the data storage area
    // reserve the first data block (for root directory)
    for (uint32_t n = 0; n <= info.dat_block; n++) {
        bitmap_set(&abm, n);
    }

    // write allocation bitmap
    for (uint32_t n = 0; n < abmblks; n++) {
        void* bmdata = minfs_bitmap_nth_block(&abm, n);
        blk = bcache_get_zero(bc, info.abm_block + n, &bdata);
        memcpy(bdata, bmdata, MINFS_BLOCK_SIZE);
        bcache_put(bc, blk, BLOCK_DIRTY);
    }

    // write inode bitmap
    for (uint32_t n = 0; n < ibmblks; n++) {
        void* bmdata = minfs_bitmap_nth_block(&ibm, n);
        blk = bcache_get_zero(bc, info.ibm_block + n, &bdata);
        memcpy(bdata, bmdata, MINFS_BLOCK_SIZE);
        bcache_put(bc, blk, BLOCK_DIRTY);
    }

    // write inodes
    for (uint32_t n = 0; n < inoblks; n++) {
        blk = bcache_get_zero(bc, info.ino_block + n, &bdata);
        bcache_put(bc, blk, BLOCK_DIRTY);
    }


    // setup root inode
    blk = bcache_get(bc, info.ino_block, &bdata);
    minfs_inode_t* ino = (void*) bdata;
    ino[MINFS_ROOT_INO].magic = MINFS_MAGIC_DIR;
    ino[MINFS_ROOT_INO].size = MINFS_BLOCK_SIZE;
    ino[MINFS_ROOT_INO].block_count = 1;
    ino[MINFS_ROOT_INO].link_count = 1;
    ino[MINFS_ROOT_INO].dirent_count = 2;
    ino[MINFS_ROOT_INO].dnum[0] = info.dat_block;
    bcache_put(bc, blk, BLOCK_DIRTY);

    blk = bcache_get_zero(bc, 0, &bdata);
    memcpy(bdata, &info, sizeof(info));
    bcache_put(bc, blk, BLOCK_DIRTY);
    return 0;
}
