// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "minfs.h"
#include "minfs-private.h"

namespace minfs {

mx_status_t MinfsChecker::GetInode(minfs_inode_t* inode, uint32_t ino) {
    if (ino >= fs_->info_.inode_count) {
        error("check: ino %u out of range (>=%u)\n",
              ino, fs_->info_.inode_count);
        return ERR_OUT_OF_RANGE;
    }
    mx_status_t status;
    uint32_t bno_of_ino = fs_->info_.ino_block + ino / kMinfsInodesPerBlock;
    uint32_t off_of_ino = (ino % kMinfsInodesPerBlock) * kMinfsInodeSize;
    if ((status = fs_->bc_->Read(bno_of_ino, inode, off_of_ino, kMinfsInodeSize)) < 0) {
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

mx_status_t MinfsChecker::GetInodeNthBno(minfs_inode_t* inode, uint32_t n,
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
    if ((iblk = fs_->bc_->Get(ibno)) == nullptr) {
        return ERR_NOT_FOUND;
    }
    uint32_t* ientry = static_cast<uint32_t*>(iblk->data());
    *bno_out = ientry[j];
    fs_->bc_->Put(mxtl::move(iblk), 0);
    return NO_ERROR;
}

// Convert 'single-block-reads' to generic reads, which may cross block
// boundaries. This function works on directories too.
mx_status_t MinfsChecker::FileRead(minfs_inode_t* inode, void* data, size_t len, size_t off) {
    if (off >= inode->size) {
        warn("file_read: offset %lu is greater than inode size (%u)\n", off, inode->size);
        conforming_ = false;
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
        if ((status = GetInodeNthBno(inode, n, &bno)) != NO_ERROR) {
            return status;
        }

        if ((status = fs_->bc_->Read(bno, data, adjust, xfer)) != NO_ERROR) {
            return status;
        }

        adjust = 0;
        len -= xfer;
        data = (void*)((uintptr_t)data + xfer);
        n++;
    }

    return static_cast<mx_status_t>((uintptr_t) data - (uintptr_t) start);
}

// Convert 'single-block-writes' to generic writes, which may cross block
// boundaries. This function works on directories too.
mx_status_t MinfsChecker::FileWrite(minfs_inode_t* inode, const void* data,
                                    size_t len, size_t off) {
    if (off >= inode->size) {
        warn("file_write: offset %lu is greater than inode size (%u)\n", off, inode->size);
        conforming_ = false;
        return 0;
    }
    if (len > (inode->size - off)) {
        len = inode->size - off;
    }

    const void* start = data;
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
        if ((status = GetInodeNthBno(inode, n, &bno)) != NO_ERROR) {
            return status;
        }

        if ((status = fs_->bc_->Write(bno, data, adjust, xfer)) != NO_ERROR) {
            return status;
        }

        adjust = 0;
        len -= xfer;
        data = (void*)((uintptr_t)data + xfer);
        n++;
    }

    return static_cast<mx_status_t>((uintptr_t) data - (uintptr_t) start);
}

mx_status_t MinfsChecker::CheckDirectory(minfs_inode_t* inode, uint32_t ino,
                                         uint32_t parent, uint32_t flags) {
    unsigned eno = 0;
    bool dot = false;
    bool dotdot = false;
    uint32_t dirent_count = 0;

    size_t prev_off = 0;
    size_t off = 0;
    while (true) {
        uint32_t data[MINFS_DIRENT_SIZE];
        mx_status_t status = FileRead(inode, data, MINFS_DIRENT_SIZE, off);
        if (status != MINFS_DIRENT_SIZE) {
            error("check: ino#%u: Could not read de[%u] at %zd\n", eno, ino, off);
            if (inode->dirent_count >= 2 && inode->dirent_count == eno - 1) {
                // So we couldn't read the last direntry, for whatever reason, but our
                // inode says that we shouldn't have been able to read it anyway.
                error("check: de count (%u) > inode_dirent_count (%u)\n", eno, inode->dirent_count);
                error("This directory and its inode disagree; the directory contents indicate\n"
                      "there might be more contents, but the inode says that the last entry\n"
                      "should already be marked as last.\n\n"
                      "Mark the directory as holding [%u] entries? (DEFAULT: y) [y/n] > ",
                      inode->dirent_count);
                int c = getchar();
                if (c == 'y') {
                    // Mark the 'last' visible direntry as last.
                    status = FileRead(inode, data, MINFS_DIRENT_SIZE, prev_off);
                    if (status != MINFS_DIRENT_SIZE) {
                        error("check: Error trying to update last dirent as 'last': %d.\n"
                              "Can't read the last dirent even though we just did earlier.\n",
                              status);
                        return status < 0 ? status : ERR_IO;
                    }
                    minfs_dirent_t* de = reinterpret_cast<minfs_dirent_t*>(data);
                    de->reclen |= kMinfsReclenLast;
                    FileWrite(inode, data, MINFS_DIRENT_SIZE, prev_off);
                    return NO_ERROR;
                } else {
                    return ERR_IO;
                }
            } else {
                return status < 0 ? status : ERR_IO;
            }
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
            // Re-read the dirent to acquire the full name
            uint32_t record_full[DirentSize(NAME_MAX)];
            status = FileRead(inode, record_full, DirentSize(de->namelen), off);
            if (status != static_cast<mx_status_t>(DirentSize(de->namelen))) {
                error("check: Error reading dirent of size: %u\n", DirentSize(de->namelen));
                return ERR_IO;
            }
            de = reinterpret_cast<minfs_dirent_t*>(record_full);

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
                info("ino#%u: de[%u]: ino=%u type=%u '%.*s' %s\n",
                     ino, eno, de->ino, de->type, de->namelen, de->name, is_last ? "[last]" : "");
            }
            if (flags & CD_RECURSE) {
                if ((status = CheckInode(de->ino, ino)) < 0) {
                    return status;
                }
            }
            dirent_count++;
        }
        if (is_last) {
            break;
        } else {
            prev_off = off;
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

const char* MinfsChecker::CheckDataBlock(uint32_t bno) {
    if (bno < fs_->info_.dat_block) {
        return "in metadata area";
    }
    if (bno >= fs_->info_.block_count) {
        return "out of range";
    }
    if (!fs_->block_map_.Get(bno, bno + 1)) {
        return "not allocated";
    }
    if (checked_blocks_.Get(bno, bno + 1)) {
        return "double-allocated";
    }
    checked_blocks_.Set(bno, bno + 1);
    return nullptr;
}

mx_status_t MinfsChecker::CheckFile(minfs_inode_t* inode, uint32_t ino) {
    info("Direct blocks: \n");
    for (unsigned n = 0; n < kMinfsDirect; n++) {
        info(" %d,", inode->dnum[n]);
    }
    info(" ...\n");

    uint32_t blocks = 0;

    // count and sanity-check indirect blocks
    for (unsigned n = 0; n < kMinfsIndirect; n++) {
        if (inode->inum[n]) {
            const char* msg;
            if ((msg = CheckDataBlock(inode->inum[n])) != nullptr) {
                warn("check: ino#%u: indirect block %u(@%u): %s\n",
                     ino, n, inode->inum[n], msg);
                conforming_ = false;
            }
            blocks++;
        }
    }

    // count and sanity-check data blocks

    unsigned blocks_allocated = 0;
    for (unsigned n = 0;;n++) {
        mx_status_t status;
        uint32_t bno;
        if ((status = GetInodeNthBno(inode, n, &bno)) < 0) {
            if (status == ERR_OUT_OF_RANGE) {
                break;
            } else {
                return status;
            }
        }
        if (bno) {
            blocks++;
            const char* msg;
            if ((msg = CheckDataBlock(bno)) != nullptr) {
                warn("check: ino#%u: block %u(@%u): %s\n", ino, n, bno, msg);
                conforming_ = false;
            }
            blocks_allocated = n + 1;
        }
    }
    if (blocks_allocated) {
        unsigned max_blocks = mxtl::roundup(inode->size, kMinfsBlockSize) / kMinfsBlockSize;
        if (blocks_allocated > max_blocks) {
            warn("check: ino#%u: filesize too small\n", ino);
            conforming_ = false;
        }
    }
    if (blocks != inode->block_count) {
        warn("check: ino#%u: block count %u, actual blocks %u\n",
             ino, inode->block_count, blocks);
        conforming_ = false;
    }
    return NO_ERROR;
}

mx_status_t MinfsChecker::CheckInode(uint32_t ino, uint32_t parent) {
    if (checked_inodes_.Get(ino, ino + 1)) {
        // we've been here before
        return NO_ERROR;
    }
    checked_inodes_.Set(ino, ino + 1);
    if (!fs_->inode_map_.Get(ino, ino + 1)) {
        warn("check: ino#%u: not marked in-use\n", ino);
        conforming_ = false;
    }
    mx_status_t status;
    minfs_inode_t inode;
    if ((status = GetInode(&inode, ino)) < 0) {
        error("check: ino#%u: not readable\n", ino);
        return status;
    }
    if (inode.magic == kMinfsMagicDir) {
        info("ino#%u: DIR blks=%u links=%u\n",
             ino, inode.block_count, inode.link_count);
        if ((status = CheckFile(&inode, ino)) < 0) {
            return status;
        }
        if ((status = CheckDirectory(&inode, ino, parent, CD_DUMP)) < 0) {
            return status;
        }
        if ((status = CheckDirectory(&inode, ino, parent, CD_RECURSE)) < 0) {
            return status;
        }
    } else {
        info("ino#%u: FILE blks=%u links=%u size=%u\n",
             ino, inode.block_count, inode.link_count, inode.size);
        if ((status = CheckFile(&inode, ino)) < 0) {
            return status;
        }
    }
    return NO_ERROR;
}

mx_status_t MinfsChecker::CheckForUnusedBlocks() const {
    unsigned missing = 0;
    for (unsigned n = fs_->info_.dat_block; n < fs_->info_.block_count; n++) {
        if (fs_->block_map_.Get(n, n + 1)) {
            if (!checked_blocks_.Get(n, n + 1)) {
                missing++;
            }
        }
    }
    if (missing) {
        error("check: %u allocated block%s not in use\n",
              missing, missing > 1 ? "s" : "");
        return ERR_BAD_STATE;
    }
    return NO_ERROR;
}

mx_status_t MinfsChecker::CheckForUnusedInodes() const {
    unsigned missing = 0;
    for (unsigned n = 1; n < fs_->info_.inode_count; n++) {
        if (fs_->inode_map_.Get(n, n + 1)) {
            if (!checked_inodes_.Get(n, n + 1)) {
                missing++;
            }
        }
    }
    if (missing) {
        error("check: %u allocated inode%s not in use\n",
              missing, missing > 1 ? "s" : "");
        return ERR_BAD_STATE;
    }
    return NO_ERROR;
}

MinfsChecker::MinfsChecker() : conforming_(true), fs_(nullptr) {};

mx_status_t MinfsChecker::Init(Bcache* bc, const minfs_info_t* info) {
    mx_status_t status;
    if ((status = checked_inodes_.Reset(info->inode_count)) < 0) {
        return status;
    }
    if ((status = checked_blocks_.Reset(info->block_count)) < 0) {
        return status;
    }
    Minfs* fs;
    if ((status = Minfs::Create(&fs, bc, info)) < 0) {
        return status;
    }
    fs_.reset(fs);
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

    MinfsChecker chk;
    if ((status = chk.Init(bc, &info)) != NO_ERROR) {
        return status;
    }

    //TODO: check root not a directory
    if ((status = chk.CheckInode(1, 1)) < 0) {
        return status;
    }

    status = NO_ERROR;
    mx_status_t r;

    // Save an error if it occurs, but check for subsequent errors
    // anyway.
    r = chk.CheckForUnusedBlocks();
    status |= (status != NO_ERROR) ? 0 : r;
    r = chk.CheckForUnusedInodes();
    status |= (status != NO_ERROR) ? 0 : r;

    //TODO: check allocated inodes that were abandoned
    //TODO: check allocated blocks that were not accounted for
    //TODO: check unallocated inodes where magic != 0
    status |= (status != NO_ERROR) ? 0 : (chk.conforming_ ? NO_ERROR : ERR_BAD_STATE);
    return status;
}

} // namespace minfs
