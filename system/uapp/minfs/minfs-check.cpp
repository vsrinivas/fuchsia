// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "minfs-private.h"
#include "minfs.h"

namespace minfs {

mx_status_t MinfsChecker::GetInode(minfs_inode_t* inode, uint32_t ino) {
    if (ino >= fs_->info_.inode_count) {
        error("check: ino %u out of range (>=%u)\n",
              ino, fs_->info_.inode_count);
        return ERR_OUT_OF_RANGE;
    }
    uint32_t bno_of_ino = ino / kMinfsInodesPerBlock;
    uint32_t off_of_ino = (ino % kMinfsInodesPerBlock) * kMinfsInodeSize;
    uintptr_t iaddr = reinterpret_cast<uintptr_t>(fs_->inode_table_->GetData()) +
                      bno_of_ino * kMinfsBlockSize + off_of_ino;
    memcpy(inode, reinterpret_cast<void*>(iaddr), kMinfsInodeSize);
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
    char data[kMinfsBlockSize];
    mx_status_t status;
    if ((status = fs_->bc_->Readblk(ibno, data)) != NO_ERROR) {
        return status;
    }
    uint32_t* ientry = reinterpret_cast<uint32_t*>(data);
    *bno_out = ientry[j];
    return NO_ERROR;
}

mx_status_t MinfsChecker::CheckDirectory(minfs_inode_t* inode, uint32_t ino,
                                         uint32_t parent, uint32_t flags) {
    unsigned eno = 0;
    bool dot = false;
    bool dotdot = false;
    uint32_t dirent_count = 0;

    mx_status_t status;
    mxtl::RefPtr<VnodeMinfs> vn;
    if ((status = VnodeMinfs::AllocateHollow(fs_.get(), &vn)) != NO_ERROR) {
        return status;
    }
    memcpy(&vn->inode_, inode, kMinfsInodeSize);
    vn->ino_ = ino;

    size_t prev_off = 0;
    size_t off = 0;
    while (true) {
        uint32_t data[MINFS_DIRENT_SIZE];
        size_t actual;
        status = vn->ReadInternal(data, MINFS_DIRENT_SIZE, off, &actual);
        if (status != NO_ERROR || actual != MINFS_DIRENT_SIZE) {
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
                    status = vn->ReadInternal(data, MINFS_DIRENT_SIZE, prev_off, &actual);
                    if (status != NO_ERROR || actual != MINFS_DIRENT_SIZE) {
                        error("check: Error trying to update last dirent as 'last': %d.\n"
                              "Can't read the last dirent even though we just did earlier.\n",
                              status);
                        return status < 0 ? status : ERR_IO;
                    }
                    minfs_dirent_t* de = reinterpret_cast<minfs_dirent_t*>(data);
                    de->reclen |= kMinfsReclenLast;
                    WriteTxn txn(fs_->bc_.get());
                    vn->WriteInternal(&txn, data, MINFS_DIRENT_SIZE, prev_off, &actual);
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
            status = vn->ReadInternal(record_full, DirentSize(de->namelen), off, &actual);
            if (status != NO_ERROR || actual != DirentSize(de->namelen)) {
                error("check: Error reading dirent of size: %u\n", DirentSize(de->namelen));
                return ERR_IO;
            }
            de = reinterpret_cast<minfs_dirent_t*>(record_full);
            bool dot_or_dotdot = false;

            if ((de->namelen == 0) || (de->namelen > (rlen - MINFS_DIRENT_SIZE))) {
                error("check: ino#%u: de[%u]: invalid namelen %u\n", ino, eno, de->namelen);
                return ERR_IO_DATA_INTEGRITY;
            }
            if ((de->namelen == 1) && (de->name[0] == '.')) {
                if (dot) {
                    error("check: ino#%u: multiple '.' entries\n", ino);
                }
                dot_or_dotdot = true;
                dot = true;
                if (de->ino != ino) {
                    error("check: ino#%u: de[%u]: '.' ino=%u (not self!)\n", ino, eno, de->ino);
                }
            }
            if ((de->namelen == 2) && (de->name[0] == '.') && (de->name[1] == '.')) {
                if (dotdot) {
                    error("check: ino#%u: multiple '..' entries\n", ino);
                }
                dot_or_dotdot = true;
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
                if ((status = CheckInode(de->ino, ino, dot_or_dotdot)) < 0) {
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
    for (unsigned n = 0;; n++) {
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

mx_status_t MinfsChecker::CheckInode(uint32_t ino, uint32_t parent, bool dot_or_dotdot) {
    minfs_inode_t inode;
    mx_status_t status;

    if ((status = GetInode(&inode, ino)) < 0) {
        error("check: ino#%u: not readable\n", ino);
        return status;
    }

    bool prev_checked = checked_inodes_.Get(ino, ino + 1);

    if (inode.magic == kMinfsMagicDir && prev_checked && !dot_or_dotdot) {
        error("check: ino#%u: Multiple hard links to directory (excluding '.' and '..') found\n", ino);
        return ERR_BAD_STATE;
    }

    links_[ino - 1] += 1;

    if (prev_checked) {
        // we've been here before
        return NO_ERROR;
    }

    links_[ino - 1] -= inode.link_count;

    checked_inodes_.Set(ino, ino + 1);

    if (!fs_->inode_map_.Get(ino, ino + 1)) {
        warn("check: ino#%u: not marked in-use\n", ino);
        conforming_ = false;
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

mx_status_t MinfsChecker::CheckLinkCounts() const {
    unsigned error = 0;
    for (uint32_t n = 0; n < fs_->info_.inode_count; n++) {
        if (links_[n] != 0) {
            error += 1;
            error("check: inode#%u has incorrect link count %u\n", n + 1, links_[n]);
            return ERR_BAD_STATE;
        }
    }
    if (error) {
        error("check: %u inode%s with incorrect link count\n",
              error, error > 1 ? "s" : "");
        return ERR_BAD_STATE;
    }
    return NO_ERROR;
}

MinfsChecker::MinfsChecker()
    : conforming_(true), fs_(nullptr), links_(){};

mx_status_t MinfsChecker::Init(mxtl::unique_ptr<Bcache> bc, const minfs_info_t* info) {
    links_.reset(new int32_t[info->inode_count]{0}, info->inode_count);
    links_[0] = -1;

    mx_status_t status;
    if ((status = checked_inodes_.Reset(info->inode_count)) < 0) {
        return status;
    }
    if ((status = checked_blocks_.Reset(info->block_count)) < 0) {
        return status;
    }
    Minfs* fs;
    if ((status = Minfs::Create(&fs, mxtl::move(bc), info)) < 0) {
        return status;
    }
    fs_.reset(fs);
    return NO_ERROR;
}

mx_status_t minfs_check(mxtl::unique_ptr<Bcache> bc) {
    mx_status_t status;

    char data[kMinfsBlockSize];
    if (bc->Readblk(0, data) < 0) {
        error("minfs: could not read info block\n");
        return -1;
    }
    const minfs_info_t* info = reinterpret_cast<const minfs_info_t*>(data);
    minfs_dump_info(info);
    if (minfs_check_info(info, bc->Maxblk())) {
        return -1;
    }

    MinfsChecker chk;
    if ((status = chk.Init(mxtl::move(bc), info)) != NO_ERROR) {
        return status;
    }

    //TODO: check root not a directory
    if ((status = chk.CheckInode(1, 1, 0)) < 0) {
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
    r = chk.CheckLinkCounts();
    status |= (status != NO_ERROR) ? 0 : r;

    //TODO: check allocated inodes that were abandoned
    //TODO: check allocated blocks that were not accounted for
    //TODO: check unallocated inodes where magic != 0
    status |= (status != NO_ERROR) ? 0 : (chk.conforming_ ? NO_ERROR : ERR_BAD_STATE);
    return status;
}

} // namespace minfs
