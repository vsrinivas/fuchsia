// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "minfs.h"
#include "minfs-private.h"

#define VERBOSE 1

namespace minfs {
namespace {

mx_status_t get_inode(const Minfs* fs, minfs_inode_t* inode, uint32_t ino) {
    if (ino >= fs->info_.inode_count) {
        error("check: ino %u out of range (>=%u)\n",
              ino, fs->info_.inode_count);
        return ERR_OUT_OF_RANGE;
    }
    mx_status_t status;
    uint32_t bno_of_ino = fs->info_.ino_block + ino / kMinfsInodesPerBlock;
    uint32_t off_of_ino = (ino % kMinfsInodesPerBlock) * kMinfsInodeSize;
    if ((status = fs->bc_->Read(bno_of_ino, inode, off_of_ino, kMinfsInodeSize)) < 0) {
        return status;
    }
    if ((inode->magic != kMinfsMagicFile) && (inode->magic != kMinfsMagicDir)) {
        error("check: ino %u has bad magic %#x\n", ino, inode->magic);
        return ERR_IO_DATA_INTEGRITY;
    }
    return NO_ERROR;
}

#define CD_DUMP 1
#define CD_RECURSE 2

mx_status_t get_inode_nth_bno(const Minfs* fs, minfs_inode_t* inode, uint32_t n,
                              uint32_t* bno_out) {
    if (n < kMinfsDirect) {
        *bno_out = inode->dnum[n];
        return NO_ERROR;
    }
    n -= kMinfsDirect;
    uint32_t i = static_cast<uint32_t>(n / (kMinfsBlockSize / sizeof(uint32_t)));
    uint32_t j = n % (kMinfsBlockSize / sizeof(uint32_t));

    if (i >= kMinfsIndirect) {
        return ERR_OUT_OF_RANGE;
    }

    uint32_t ibno;
    if ((ibno = inode->inum[i]) == 0) {
        *bno_out = 0;
        return NO_ERROR;
    }
    mxtl::RefPtr<BlockNode> iblk;
    if ((iblk = fs->bc_->Get(ibno)) == nullptr) {
        return ERR_NOT_FOUND;
    }
    uint32_t* ientry = static_cast<uint32_t*>(iblk->data());
    *bno_out = ientry[j];
    fs->bc_->Put(mxtl::move(iblk), 0);
    return NO_ERROR;
}

// Convert 'single-block-reads' to generic reads, which may cross block
// boundaries. This function works on directories too.
mx_status_t file_read(const Minfs* fs, minfs_inode_t* inode, void* data, size_t len, size_t off) {
    if (off >= inode->size) {
        return 0;
    }
    if (len > (inode->size - off)) {
        len = inode->size - off;
    }

    void* start = data;
    uint32_t n = static_cast<uint32_t>(off / kMinfsBlockSize);
    uint32_t adjust = off % kMinfsBlockSize;

    while ((len > 0) && (n < kMinfsMaxFileBlock)) {
        uint32_t xfer;
        if (len > (kMinfsBlockSize - adjust)) {
            xfer = kMinfsBlockSize - adjust;
        } else {
            xfer = static_cast<uint32_t>(len);
        }

        uint32_t bno;
        mx_status_t status;
        if ((status = get_inode_nth_bno(fs, inode, n, &bno)) != NO_ERROR) {
            return status;
        }

        if ((status = fs->bc_->Read(bno, data, adjust, xfer)) != NO_ERROR) {
            return status;
        }

        adjust = 0;
        len -= xfer;
        data = (void*)((uintptr_t)data + xfer);
        n++;
    }

    return static_cast<mx_status_t>((uintptr_t) data - (uintptr_t) start);
}

mx_status_t check_directory(CheckMaps* chk, const Minfs* fs, minfs_inode_t* inode,
                            uint32_t ino, uint32_t parent, uint32_t flags) {
    unsigned eno = 0;
    bool dot = false;
    bool dotdot = false;
    uint32_t dirent_count = 0;

    size_t off = 0;
    while (true) {
        uint32_t data[MINFS_DIRENT_SIZE];
        mx_status_t status = file_read(fs, inode, data, MINFS_DIRENT_SIZE, off);
        if (status != MINFS_DIRENT_SIZE) {
            error("check: ino#%u: Could not read direnty at %zd\n", ino, off);
            return status < 0 ? status : ERR_IO;
        }
        minfs_dirent_t* de = reinterpret_cast<minfs_dirent_t*>(data);
        uint32_t rlen = static_cast<uint32_t>(MinfsReclen(de, off));
        bool is_last = de->reclen & kMinfsReclenLast;
        if (!is_last && ((rlen < MINFS_DIRENT_SIZE) ||
                         (rlen > kMinfsMaxDirentSize) || (rlen & 3))) {
            error("check: ino#%u: de[%u]: bad dirent reclen (%u)\n", ino, eno, rlen);
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
        if (is_last) {
            break;
        } else {
            off += rlen;
        }
        eno++;
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

const char* check_data_block(CheckMaps* chk, const Minfs* fs, uint32_t bno) {
    if (bno < fs->info_.dat_block) {
        return "in metadata area";
    }
    if (bno >= fs->info_.block_count) {
        return "out of range";
    }
    if (!fs->block_map_.Get(bno, bno + 1)) {
        return "not allocated";
    }
    if (chk->checked_blocks.Get(bno, bno + 1)) {
        return "double-allocated";
    }
    chk->checked_blocks.Set(bno, bno + 1);
    return nullptr;
}

mx_status_t check_file(CheckMaps* chk, const Minfs* fs,
                       minfs_inode_t* inode, uint32_t ino) {
#if VERBOSE
    for (unsigned n = 0; n < kMinfsDirect; n++) {
        info("%d, ", inode->dnum[n]);
    }
    info("...\n");
#endif

    uint32_t blocks = 0;

    // count and sanity-check indirect blocks
    for (unsigned n = 0; n < kMinfsIndirect; n++) {
        if (inode->inum[n]) {
            const char* msg;
            if ((msg = check_data_block(chk, fs, inode->inum[n])) != nullptr) {
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
            if ((msg = check_data_block(chk, fs, bno)) != nullptr) {
                warn("check: ino#%u: block %u(@%u): %s\n", ino, n, bno, msg);
            }
            max = n + 1;
        }
    }
    if (max) {
        unsigned sizeblocks = inode->size / kMinfsBlockSize;
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

} // namespace anonymous

mx_status_t check_inode(CheckMaps* chk, const Minfs* fs, uint32_t ino, uint32_t parent) {
    if (chk->checked_inodes.Get(ino, ino + 1)) {
        // we've been here before
        return NO_ERROR;
    }
    chk->checked_inodes.Set(ino, ino + 1);
    if (!fs->inode_map_.Get(ino, ino + 1)) {
        warn("check: ino#%u: not marked in-use\n", ino);
    }
    mx_status_t status;
    minfs_inode_t inode;
    if ((status = get_inode(fs, &inode, ino)) < 0) {
        error("check: ino#%u: not readable\n", ino);
        return status;
    }
    if (inode.magic == kMinfsMagicDir) {
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

mx_status_t minfs_check(Bcache* bc) {
    mx_status_t status;

    minfs_info_t info;
    if (bc->Read(0, &info, 0, sizeof(info)) < 0) {
        error("minfs: could not read info block\n");
        return -1;
    }
    minfs_dump_info(&info);
    if (minfs_check_info(&info, bc->Maxblk())) {
        return -1;
    }

    CheckMaps chk;
    if ((status = chk.checked_inodes.Reset(info.inode_count)) < 0) {
        return status;
    }
    if ((status = chk.checked_blocks.Reset(info.block_count)) < 0) {
        return status;
    }
    Minfs* fs;
    if ((status = Minfs::Create(&fs, bc, &info)) < 0) {
        return status;
    }

    //TODO: check root not a directory
    if ((status = check_inode(&chk, fs, 1, 1)) < 0) {
        return status;
    }

    unsigned missing = 0;
    for (unsigned n = info.dat_block; n < info.block_count; n++) {
        if (fs->block_map_.Get(n, n + 1)) {
            if (!chk.checked_blocks.Get(n, n + 1)) {
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
        if (fs->inode_map_.Get(n, n + 1)) {
            if (!chk.checked_inodes.Get(n, n + 1)) {
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

} // namespace minfs
