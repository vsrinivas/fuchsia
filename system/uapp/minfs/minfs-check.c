// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "minfs-private.h"

#define VERBOSE 1

typedef struct check {
    bitmap_t checked_inodes;
    bitmap_t checked_blocks;
} check_t;

static mx_status_t check_inode(check_t* chk, minfs_t* fs, uint32_t ino, uint32_t parent);

static mx_status_t get_inode(minfs_t* fs, minfs_inode_t* inode, uint32_t ino) {
    if (ino >= fs->info.inode_count) {
        error("check: ino %u out of range (>=%u)\n",
              ino, fs->info.inode_count);
        return ERR_OUT_OF_RANGE;
    }
    mx_status_t status;
    uint32_t bno_of_ino = fs->info.ino_block + ino / MINFS_INODES_PER_BLOCK;
    uint32_t off_of_ino = (ino % MINFS_INODES_PER_BLOCK) * MINFS_INODE_SIZE;
    if ((status = bcache_read(fs->bc, bno_of_ino, inode, off_of_ino, MINFS_INODE_SIZE)) < 0) {
        return status;
    }
    if ((inode->magic != MINFS_MAGIC_FILE) && (inode->magic != MINFS_MAGIC_DIR)) {
        error("check: ino %u has bad magic %#x\n", ino, inode->magic);
        return ERR_IO_DATA_INTEGRITY;
    }
    return NO_ERROR;
}

#define CD_DUMP 1
#define CD_RECURSE 2

static mx_status_t get_inode_nth_bno(minfs_t* fs, minfs_inode_t* inode, uint32_t n, uint32_t* bno_out) {
    if (n < MINFS_DIRECT) {
        *bno_out = inode->dnum[n];
        return NO_ERROR;
    }
    n -= MINFS_DIRECT;
    uint32_t i = n / (MINFS_BLOCK_SIZE / sizeof(uint32_t));
    uint32_t j = n % (MINFS_BLOCK_SIZE / sizeof(uint32_t));

    if (i >= MINFS_INDIRECT) {
        return ERR_OUT_OF_RANGE;
    }

    uint32_t ibno;
    block_t* iblk;
    uint32_t* ientry;
    if ((ibno = inode->inum[i]) == 0) {
        *bno_out = 0;
        return NO_ERROR;
    }
    if ((iblk = bcache_get(fs->bc, ibno, (void**) &ientry)) == NULL) {
        return ERR_NOT_FOUND;
    }

    *bno_out = ientry[j];
    bcache_put(fs->bc, iblk, 0);
    return NO_ERROR;
}

static mx_status_t check_directory(check_t* chk, minfs_t* fs, minfs_inode_t* inode,
                                   uint32_t ino, uint32_t parent, uint32_t flags) {
    unsigned eno = 0;
    bool dot = false;
    bool dotdot = false;
    uint32_t dirent_count = 0;
    for (unsigned n = 0; n < inode->block_count; n++) {
        uint32_t bno;
        mx_status_t status;
        if ((status = get_inode_nth_bno(fs, inode, n, &bno)) < 0) {
            error("check: ino#%u: directory block %u invalid\n", ino, n);
            return status;
        }
        uint32_t data[MINFS_BLOCK_SIZE];
        if ((status = bcache_read(fs->bc, bno, data, 0, MINFS_BLOCK_SIZE)) < 0) {
            error("check: ino#%u: failed to read block %u (bno=%u)\n", ino, n, bno);
            return status;
        }
        uint32_t size = MINFS_BLOCK_SIZE;
        minfs_dirent_t* de = (void*) data;
        while (size > MINFS_DIRENT_SIZE) {
            uint32_t rlen = de->reclen;
            if ((rlen > size) || (rlen & 3)) {
                error("check: ino#%u: de[%u]: bad dirent reclen\n", ino, eno);
                return ERR_IO_DATA_INTEGRITY;
            }
            if (de->ino == 0) {
                if (flags & CD_DUMP) {
                    info("ino#%u: de[%u]: <empty> reclen=%u\n", ino, eno, rlen);
                }
            } else {
                if ((de->namelen == 0) || (de->namelen > (rlen - MINFS_DIRENT_SIZE))) {
                    error("check: ino#%u: de[%u]: invalid namelen %u\n", ino, eno, de->namelen);
                    return ERR_IO_DATA_INTEGRITY;
                }
                if ((de->namelen == 1) && (de->name[0] == '.')) {
                    if (dot) {
                        error("check: ino#%u: multiple '.' entries\n", ino);
                    }
                    dot = true;
                    if (de->ino != ino) {
                        error("check: ino#%u: de[%u]: '.' ino=%u (not self!)\n", ino, eno, de->ino);
                    }
                }
                if ((de->namelen == 2) && (de->name[0] == '.') && (de->name[1] == '.')) {
                    if (dotdot) {
                        error("check: ino#%u: multiple '..' entries\n", ino);
                    }
                    dotdot = true;
                    if (de->ino != parent) {
                        error("check: ino#%u: de[%u]: '..' ino=%u (not parent!)\n", ino, eno, de->ino);
                    }
                }
                //TODO: check for cycles (non-dot/dotdot dir ref already in checked bitmap)
                if (flags & CD_DUMP) {
                    info("ino#%u: de[%u]: ino=%u type=%u '%.*s'\n",
                         ino, eno, de->ino, de->type, de->namelen, de->name);
                }
                if (flags & CD_RECURSE) {
                    if ((status = check_inode(chk, fs, de->ino, ino)) < 0) {
                        return status;
                    }
                }
                dirent_count++;
            }
            eno++;
            de = ((void*) de) + rlen;
            size -= rlen;
        }
        if (size > 0) {
            error("check: ino#%u: blk=%u bno=%u dir block not full\n", ino, n, bno);
        }
    }
    if (dirent_count != inode->dirent_count) {
        error("check: ino#%u: dirent_count of %u != %u (actual)\n",
              ino, inode->dirent_count, dirent_count);
    }
    if (dot == false) {
        error("check: ino#%u: directory missing '.'\n", ino);
    }
    if (dotdot == false) {
        error("check: ino#%u: directory missing '..'\n", ino);
    }
    return NO_ERROR;
}

const char* check_data_block(check_t* chk, minfs_t* fs, uint32_t bno) {
    if (bno < fs->info.dat_block) {
        return "in metadata area";
    }
    if (bno >= fs->info.block_count) {
        return "out of range";
    }
    if (!bitmap_get(&fs->block_map, bno)) {
        return "not allocated";
    }
    if (bitmap_get(&chk->checked_blocks, bno)) {
        return "double-allocated";
    }
    bitmap_set(&chk->checked_blocks, bno);
    return NULL;
}

mx_status_t check_file(check_t* chk, minfs_t* fs,
                       minfs_inode_t* inode, uint32_t ino) {
#if VERBOSE
    for (unsigned n = 0; n < MINFS_DIRECT; n++) {
        info("%d, ", inode->dnum[n]);
    }
    info("...\n");
#endif

    uint32_t blocks = 0;

    // count and sanity-check indirect blocks
    for (unsigned n = 0; n < MINFS_INDIRECT; n++) {
        if (inode->inum[n]) {
            const char* msg;
            if ((msg = check_data_block(chk, fs, inode->inum[n])) != NULL) {
                warn("check: ino#%u: indirect block %u(@%u): %s\n",
                     ino, n, inode->inum[n], msg);
            }
            blocks++;
        }
    }

    // count and sanity-check data blocks

    unsigned max = 0;
    for (unsigned n = 0;;n++) {
        mx_status_t status;
        uint32_t bno;
        if ((status = get_inode_nth_bno(fs, inode, n, &bno)) < 0) {
            if (status == ERR_OUT_OF_RANGE) {
                break;
            } else {
                return status;
            }
        }
        if (bno) {
            blocks++;
            const char* msg;
            if ((msg = check_data_block(chk, fs, bno)) != NULL) {
                warn("check: ino#%u: block %u(@%u): %s\n", ino, n, bno, msg);
            }
            max = n + 1;
        }
    }
    if (max) {
        unsigned sizeblocks = inode->size / MINFS_BLOCK_SIZE;
        if (sizeblocks > max) {
            warn("check: ino#%u: filesize too large\n", ino);
        } else if (sizeblocks < (max - 1)) {
            warn("check: ino#%u: filesize too small\n", ino);
        }
    } else {
        if (inode->size) {
            warn("check: ino#%u: filesize too large\n", ino);
        }
    }
    if (blocks != inode->block_count) {
        warn("check: ino#%u: block count %u, actual blocks %u\n",
             ino, inode->block_count, blocks);
    }
    return NO_ERROR;
}

mx_status_t check_inode(check_t* chk, minfs_t* fs, uint32_t ino, uint32_t parent) {
    if (bitmap_get(&chk->checked_inodes, ino)) {
        // we've been here before
        return NO_ERROR;
    }
    bitmap_set(&chk->checked_inodes, ino);
    if (!bitmap_get(&fs->inode_map, ino)) {
        warn("check: ino#%u: not marked in-use\n", ino);
    }
    mx_status_t status;
    minfs_inode_t inode;
    if ((status = get_inode(fs, &inode, ino)) < 0) {
        error("check: ino#%u: not readable\n", ino);
        return status;
    }
    if (inode.magic == MINFS_MAGIC_DIR) {
        info("ino#%u: DIR blks=%u links=%u\n",
             ino, inode.block_count, inode.link_count);
        if ((status = check_file(chk, fs, &inode, ino)) < 0) {
            return status;
        }
#if VERBOSE
        if ((status = check_directory(chk, fs, &inode, ino, parent, CD_DUMP)) < 0) {
            return status;
        }
#endif
        if ((status = check_directory(chk, fs, &inode, ino, parent, CD_RECURSE)) < 0) {
            return status;
        }
    } else {
        info("ino#%u: FILE blks=%u links=%u size=%u\n",
             ino, inode.block_count, inode.link_count, inode.size);
        if ((status = check_file(chk, fs, &inode, ino)) < 0) {
            return status;
        }
    }
    return NO_ERROR;
}

mx_status_t minfs_check(bcache_t* bc) {
    mx_status_t status;

    minfs_info_t info;
    if (bcache_read(bc, 0, &info, 0, sizeof(info)) < 0) {
        error("minfs: could not read info block\n");
        return -1;
    }
    minfs_dump_info(&info);
    if (minfs_check_info(&info, bcache_max_block(bc))) {
        return -1;
    }

    check_t chk;
    if ((status = bitmap_init(&chk.checked_inodes, info.inode_count)) < 0) {
        return status;
    }
    if ((status = bitmap_init(&chk.checked_blocks, info.block_count)) < 0) {
        return status;
    }
    minfs_t* fs;
    if ((status = minfs_create(&fs, bc, &info)) < 0) {
        return status;
    }
    if (minfs_load_bitmaps(fs)) {
        return -1;
    }

    //TODO: check root not a directory
    if ((status = check_inode(&chk, fs, 1, 1)) < 0) {
        return status;
    }

    unsigned missing = 0;
    for (unsigned n = info.dat_block; n < info.block_count; n++) {
        if (bitmap_get(&fs->block_map, n)) {
            if (!bitmap_get(&chk.checked_blocks, n)) {
                missing++;
            }
        }
    }
    if (missing) {
        error("check: %u allocated block%s not in use\n",
              missing, missing > 1 ? "s" : "");
    }

    missing = 0;
    for (unsigned n = 1; n < info.inode_count; n++) {
        if (bitmap_get(&fs->inode_map, n)) {
            if (!bitmap_get(&chk.checked_inodes, n)) {
                missing++;
            }
        }
    }
    if (missing) {
        error("check: %u allocated inode%s not in use\n",
              missing, missing > 1 ? "s" : "");
    }

    //TODO: check allocated inodes that were abandoned
    //TODO: check allocated blocks that were not accounted for
    //TODO: check unallocated inodes where magic != 0
    fprintf(stderr, "check: okay\n");
    return NO_ERROR;
}
