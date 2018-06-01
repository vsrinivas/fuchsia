// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <minfs/format.h>
#include <minfs/fsck.h>
#include "minfs-private.h"

// #define DEBUG_PRINTF
#ifdef DEBUG_PRINTF
#define xprintf(args...) fprintf(stderr, args)
#else
#define xprintf(args...)
#endif

namespace minfs {

class MinfsChecker {
public:
    MinfsChecker();
    zx_status_t Init(fbl::unique_ptr<Bcache> bc, const minfs_info_t* info);
    void CheckReserved();
    zx_status_t CheckInode(ino_t ino, ino_t parent, bool dot_or_dotdot);
    zx_status_t CheckForUnusedBlocks() const;
    zx_status_t CheckForUnusedInodes() const;
    zx_status_t CheckLinkCounts() const;
    zx_status_t CheckAllocatedCounts() const;

    // "Set once"-style flag to identify if anything nonconforming
    // was found in the underlying filesystem -- even if it was fixed.
    bool conforming_;

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(MinfsChecker);

    zx_status_t GetInode(minfs_inode_t* inode, ino_t ino);

    // Returns the nth block within an inode, relative to the start of the
    // file. Returns the "next_n" which might contain a bno. This "next_n"
    // is for performance reasons -- it allows fsck to avoid repeatedly checking
    // the same indirect / doubly indirect blocks with all internal
    // bno unallocated.
    zx_status_t GetInodeNthBno(minfs_inode_t* inode, blk_t n, blk_t* next_n,
                               blk_t* bno_out);
    zx_status_t CheckDirectory(minfs_inode_t* inode, ino_t ino,
                               ino_t parent, uint32_t flags);
    const char* CheckDataBlock(blk_t bno);
    zx_status_t CheckFile(minfs_inode_t* inode, ino_t ino);

    fbl::unique_ptr<Minfs> fs_;
    RawBitmap checked_inodes_;
    RawBitmap checked_blocks_;

    uint32_t alloc_inodes_;
    uint32_t alloc_blocks_;
    fbl::Array<int32_t> links_;

    blk_t cached_doubly_indirect_;
    blk_t cached_indirect_;
    uint8_t doubly_indirect_cache_[kMinfsBlockSize];
    uint8_t indirect_cache_[kMinfsBlockSize];
};

zx_status_t MinfsChecker::GetInode(minfs_inode_t* inode, ino_t ino) {
    if (ino >= fs_->Info().inode_count) {
        FS_TRACE_ERROR("check: ino %u out of range (>=%u)\n",
              ino, fs_->Info().inode_count);
        return ZX_ERR_OUT_OF_RANGE;
    }

    fs_->inodes_->Load(ino, inode);
    if ((inode->magic != kMinfsMagicFile) && (inode->magic != kMinfsMagicDir)) {
        FS_TRACE_ERROR("check: ino %u has bad magic %#x\n", ino, inode->magic);
        return ZX_ERR_IO_DATA_INTEGRITY;
    }
    return ZX_OK;
}

#define CD_DUMP 1
#define CD_RECURSE 2

zx_status_t MinfsChecker::GetInodeNthBno(minfs_inode_t* inode, blk_t n,
                                         blk_t* next_n, blk_t* bno_out) {
    // The default value for the "next n". It's easier to set it here anyway,
    // since we proceed to modify n in the code below.
    *next_n = n + 1;
    if (n < kMinfsDirect) {
        *bno_out = inode->dnum[n];
        return ZX_OK;
    }

    n -= kMinfsDirect;
    uint32_t i = n / kMinfsDirectPerIndirect; // indirect index
    uint32_t j = n % kMinfsDirectPerIndirect; // direct index

    if (i < kMinfsIndirect) {
        blk_t ibno;
        if ((ibno = inode->inum[i]) == 0) {
            *bno_out = 0;
            *next_n = kMinfsDirect + (i + 1) * kMinfsDirectPerIndirect;
            return ZX_OK;
        }

        if (cached_indirect_ != ibno) {
            zx_status_t status;
            if ((status = fs_->ReadDat(ibno, indirect_cache_)) != ZX_OK) {
                return status;
            }
            cached_indirect_ = ibno;
        }

        uint32_t* ientry = reinterpret_cast<uint32_t*>(indirect_cache_);
        *bno_out = ientry[j];
        return ZX_OK;
    }

    n -= kMinfsIndirect * kMinfsDirectPerIndirect;
    i = n / (kMinfsDirectPerDindirect); // doubly indirect index
    n -= (i * kMinfsDirectPerDindirect);
    j = n / kMinfsDirectPerIndirect; // indirect index
    uint32_t k = n % kMinfsDirectPerIndirect; // direct index

    if (i < kMinfsDoublyIndirect) {
        blk_t dibno;
        if ((dibno = inode->dinum[i]) == 0) {
            *bno_out = 0;
            *next_n = kMinfsDirect + kMinfsIndirect * kMinfsDirectPerIndirect +
                    (i + 1) * kMinfsDirectPerDindirect;
            return ZX_OK;
        }

        if (cached_doubly_indirect_ != dibno) {
            zx_status_t status;
            if ((status = fs_->ReadDat(dibno, doubly_indirect_cache_)) != ZX_OK) {
                return status;
            }
            cached_doubly_indirect_ = dibno;
        }

        uint32_t* dientry = reinterpret_cast<uint32_t*>(doubly_indirect_cache_);
        blk_t ibno;
        if ((ibno = dientry[j]) == 0) {
            *bno_out = 0;
            *next_n = kMinfsDirect + kMinfsIndirect * kMinfsDirectPerIndirect +
                    (i * kMinfsDirectPerDindirect) + (j + 1) * kMinfsDirectPerIndirect;
            return ZX_OK;
        }

        if (cached_indirect_ != ibno) {
            zx_status_t status;
            if ((status = fs_->ReadDat(ibno, indirect_cache_)) != ZX_OK) {
                return status;
            }
            cached_indirect_ = ibno;
        }

        uint32_t* ientry = reinterpret_cast<uint32_t*>(indirect_cache_);
        *bno_out = ientry[k];
        return ZX_OK;
    }

    return ZX_ERR_OUT_OF_RANGE;
}

zx_status_t MinfsChecker::CheckDirectory(minfs_inode_t* inode, ino_t ino,
                                         ino_t parent, uint32_t flags) {
    unsigned eno = 0;
    bool dot = false;
    bool dotdot = false;
    uint32_t dirent_count = 0;

    zx_status_t status;
    fbl::RefPtr<VnodeMinfs> vn;
    if ((status = VnodeMinfs::Recreate(fs_.get(), ino, &vn)) != ZX_OK) {
        return status;
    }

    size_t off = 0;
    while (true) {
        uint32_t data[MINFS_DIRENT_SIZE];
        size_t actual;
        status = vn->ReadInternal(data, MINFS_DIRENT_SIZE, off, &actual);
        if (status != ZX_OK || actual != MINFS_DIRENT_SIZE) {
            FS_TRACE_ERROR("check: ino#%u: Could not read de[%u] at %zd\n", eno, ino, off);
            if (inode->dirent_count >= 2 && inode->dirent_count == eno - 1) {
                // So we couldn't read the last direntry, for whatever reason, but our
                // inode says that we shouldn't have been able to read it anyway.
                FS_TRACE_ERROR("check: de count (%u) > inode_dirent_count (%u)\n", eno,
                               inode->dirent_count);
            }
            return status != ZX_OK ? status : ZX_ERR_IO;
        }
        minfs_dirent_t* de = reinterpret_cast<minfs_dirent_t*>(data);
        uint32_t rlen = static_cast<uint32_t>(MinfsReclen(de, off));
        uint32_t dlen = DirentSize(de->namelen);
        bool is_last = de->reclen & kMinfsReclenLast;
        if (!is_last && ((rlen < MINFS_DIRENT_SIZE) || (dlen > rlen) ||
                         (dlen > kMinfsMaxDirentSize) || (rlen & 3))) {
            FS_TRACE_ERROR("check: ino#%u: de[%u]: bad dirent reclen (%u)\n", ino, eno, rlen);
            return ZX_ERR_IO_DATA_INTEGRITY;
        }
        if (de->ino == 0) {
            if (flags & CD_DUMP) {
                xprintf("ino#%u: de[%u]: <empty> reclen=%u\n", ino, eno, rlen);
            }
        } else {
            // Re-read the dirent to acquire the full name
            uint32_t record_full[DirentSize(NAME_MAX)];
            status = vn->ReadInternal(record_full, DirentSize(de->namelen), off, &actual);
            if (status != ZX_OK || actual != DirentSize(de->namelen)) {
                FS_TRACE_ERROR("check: Error reading dirent of size: %u\n", DirentSize(de->namelen));
                return ZX_ERR_IO;
            }
            de = reinterpret_cast<minfs_dirent_t*>(record_full);
            bool dot_or_dotdot = false;

            if ((de->namelen == 0) || (de->namelen > (rlen - MINFS_DIRENT_SIZE))) {
                FS_TRACE_ERROR("check: ino#%u: de[%u]: invalid namelen %u\n", ino, eno, de->namelen);
                return ZX_ERR_IO_DATA_INTEGRITY;
            }
            if ((de->namelen == 1) && (de->name[0] == '.')) {
                if (dot) {
                    FS_TRACE_ERROR("check: ino#%u: multiple '.' entries\n", ino);
                }
                dot_or_dotdot = true;
                dot = true;
                if (de->ino != ino) {
                    FS_TRACE_ERROR("check: ino#%u: de[%u]: '.' ino=%u (not self!)\n", ino, eno, de->ino);
                }
            }
            if ((de->namelen == 2) && (de->name[0] == '.') && (de->name[1] == '.')) {
                if (dotdot) {
                    FS_TRACE_ERROR("check: ino#%u: multiple '..' entries\n", ino);
                }
                dot_or_dotdot = true;
                dotdot = true;
                if (de->ino != parent) {
                    FS_TRACE_ERROR("check: ino#%u: de[%u]: '..' ino=%u (not parent!)\n", ino, eno, de->ino);
                }
            }
            //TODO: check for cycles (non-dot/dotdot dir ref already in checked bitmap)
            if (flags & CD_DUMP) {
                xprintf("ino#%u: de[%u]: ino=%u type=%u '%.*s' %s\n", ino, eno, de->ino, de->type,
                        de->namelen, de->name, is_last ? "[last]" : "");
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
            off += rlen;
        }
        eno++;
    }
    if (dirent_count != inode->dirent_count) {
        FS_TRACE_ERROR("check: ino#%u: dirent_count of %u != %u (actual)\n",
              ino, inode->dirent_count, dirent_count);
    }
    if (dot == false) {
        FS_TRACE_ERROR("check: ino#%u: directory missing '.'\n", ino);
    }
    if (dotdot == false) {
        FS_TRACE_ERROR("check: ino#%u: directory missing '..'\n", ino);
    }
    return ZX_OK;
}

const char* MinfsChecker::CheckDataBlock(blk_t bno) {
    if (bno == 0) {
        return "reserved bno";
    }
    if (bno >= fs_->Info().block_count) {
        return "out of range";
    }
    if (!fs_->block_allocator_->map_.Get(bno, bno + 1)) {
        return "not allocated";
    }
    if (checked_blocks_.Get(bno, bno + 1)) {
        return "double-allocated";
    }
    checked_blocks_.Set(bno, bno + 1);
    alloc_blocks_++;
    return nullptr;
}

zx_status_t MinfsChecker::CheckFile(minfs_inode_t* inode, ino_t ino) {
    xprintf("Direct blocks: \n");
    for (unsigned n = 0; n < kMinfsDirect; n++) {
        xprintf(" %d,", inode->dnum[n]);
    }
    xprintf(" ...\n");

    uint32_t block_count = 0;

    // count and sanity-check indirect blocks
    for (unsigned n = 0; n < kMinfsIndirect; n++) {
        if (inode->inum[n]) {
            const char* msg;
            if ((msg = CheckDataBlock(inode->inum[n])) != nullptr) {
                FS_TRACE_WARN("check: ino#%u: indirect block %u(@%u): %s\n",
                     ino, n, inode->inum[n], msg);
                conforming_ = false;
            }
            block_count++;
        }
    }

    // count and sanity-check doubly indirect blocks
    for (unsigned n = 0; n < kMinfsDoublyIndirect; n++) {
        if (inode->dinum[n]) {
            const char* msg;
            if ((msg = CheckDataBlock(inode->dinum[n])) != nullptr) {
                FS_TRACE_WARN("check: ino#%u: doubly indirect block %u(@%u): %s\n",
                     ino, n, inode->dinum[n], msg);
                conforming_ = false;
            }
            block_count++;

            char data[kMinfsBlockSize];
            zx_status_t status;
            if ((status = fs_->ReadDat(inode->dinum[n], data)) != ZX_OK) {
                return status;
            }
            uint32_t* entry = reinterpret_cast<uint32_t*>(data);

            for (unsigned m = 0; m < kMinfsDirectPerIndirect; m++) {
                if (entry[m]) {
                    if ((msg = CheckDataBlock(entry[m])) != nullptr) {
                        FS_TRACE_WARN("check: ino#%u: indirect block (in dind) %u(@%u): %s\n",
                            ino, m, entry[m], msg);
                        conforming_ = false;
                    }
                    block_count++;
                }
            }
        }
    }

    // count and sanity-check data blocks

    // The next block which would be allocated if we expand the file size
    // by a single block.
    unsigned next_blk = 0;
    cached_doubly_indirect_ = 0;
    cached_indirect_ = 0;

    blk_t n = 0;
    while (true) {
        zx_status_t status;
        blk_t bno;
        blk_t next_n;
        if ((status = GetInodeNthBno(inode, n, &next_n, &bno)) < 0) {
            if (status == ZX_ERR_OUT_OF_RANGE) {
                break;
            } else {
                return status;
            }
        }
        assert(next_n > n);
        if (bno) {
            next_blk = n + 1;
            block_count++;
            const char* msg;
            if ((msg = CheckDataBlock(bno)) != nullptr) {
                FS_TRACE_WARN("check: ino#%u: block %u(@%u): %s\n", ino, n, bno, msg);
                conforming_ = false;
            }
        }
        n = next_n;
    }
    if (next_blk) {
        unsigned max_blocks = fbl::round_up(inode->size, kMinfsBlockSize) / kMinfsBlockSize;
        if (next_blk > max_blocks) {
            FS_TRACE_WARN("check: ino#%u: filesize too small\n", ino);
            conforming_ = false;
        }
    }
    if (block_count != inode->block_count) {
        FS_TRACE_WARN("check: ino#%u: block count %u, actual blocks %u\n",
             ino, inode->block_count, block_count);
        conforming_ = false;
    }
    return ZX_OK;
}

void MinfsChecker::CheckReserved() {
    // Check reserved inode '0'.
    if (fs_->inodes_->inode_allocator_->map_.Get(0, 1)) {
        checked_inodes_.Set(0, 1);
        alloc_inodes_++;
    } else {
        FS_TRACE_WARN("check: reserved inode#0: not marked in-use\n");
        conforming_ = false;
    }

    // Check reserved data block '0'.
    if (fs_->block_allocator_->map_.Get(0, 1)) {
        checked_blocks_.Set(0, 1);
        alloc_blocks_++;
    } else {
        FS_TRACE_WARN("check: reserved block#0: not marked in-use\n");
        conforming_ = false;
    }
}

zx_status_t MinfsChecker::CheckInode(ino_t ino, ino_t parent, bool dot_or_dotdot) {
    minfs_inode_t inode;
    zx_status_t status;

    if ((status = GetInode(&inode, ino)) < 0) {
        FS_TRACE_ERROR("check: ino#%u: not readable\n", ino);
        return status;
    }

    bool prev_checked = checked_inodes_.Get(ino, ino + 1);

    if (inode.magic == kMinfsMagicDir && prev_checked && !dot_or_dotdot) {
        FS_TRACE_ERROR("check: ino#%u: Multiple hard links to directory (excluding '.' and '..') found\n", ino);
        return ZX_ERR_BAD_STATE;
    }

    links_[ino - 1] += 1;

    if (prev_checked) {
        // we've been here before
        return ZX_OK;
    }

    links_[ino - 1] -= inode.link_count;
    checked_inodes_.Set(ino, ino + 1);
    alloc_inodes_++;

    if (!fs_->inodes_->inode_allocator_->map_.Get(ino, ino + 1)) {
       FS_TRACE_WARN("check: ino#%u: not marked in-use\n", ino);
        conforming_ = false;
    }

    if (inode.magic == kMinfsMagicDir) {
        xprintf("ino#%u: DIR blks=%u links=%u\n", ino, inode.block_count, inode.link_count);
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
        xprintf("ino#%u: FILE blks=%u links=%u size=%u\n", ino, inode.block_count, inode.link_count,
                inode.size);
        if ((status = CheckFile(&inode, ino)) < 0) {
            return status;
        }
    }
    return ZX_OK;
}

zx_status_t MinfsChecker::CheckForUnusedBlocks() const {
    unsigned missing = 0;

    for (unsigned n = 0; n < fs_->Info().block_count; n++) {
        if (fs_->block_allocator_->map_.Get(n, n + 1)) {
            if (!checked_blocks_.Get(n, n + 1)) {
                missing++;
            }
        }
    }
    if (missing) {
        FS_TRACE_ERROR("check: %u allocated block%s not in use\n",
              missing, missing > 1 ? "s" : "");
        return ZX_ERR_BAD_STATE;
    }
    return ZX_OK;
}

zx_status_t MinfsChecker::CheckForUnusedInodes() const {
    unsigned missing = 0;
    for (unsigned n = 0; n < fs_->Info().inode_count; n++) {
        if (fs_->inodes_->inode_allocator_->map_.Get(n, n + 1)) {
            if (!checked_inodes_.Get(n, n + 1)) {
                missing++;
            }
        }
    }
    if (missing) {
        FS_TRACE_ERROR("check: %u allocated inode%s not in use\n",
              missing, missing > 1 ? "s" : "");
        return ZX_ERR_BAD_STATE;
    }
    return ZX_OK;
}

zx_status_t MinfsChecker::CheckLinkCounts() const {
    unsigned error = 0;
    for (uint32_t n = 0; n < fs_->Info().inode_count; n++) {
        if (links_[n] != 0) {
            error += 1;
            FS_TRACE_ERROR("check: inode#%u has incorrect link count %u\n", n + 1, links_[n]);
            return ZX_ERR_BAD_STATE;
        }
    }
    if (error) {
        FS_TRACE_ERROR("check: %u inode%s with incorrect link count\n",
              error, error > 1 ? "s" : "");
        return ZX_ERR_BAD_STATE;
    }
    return ZX_OK;
}

zx_status_t MinfsChecker::CheckAllocatedCounts() const {
    zx_status_t status = ZX_OK;
    if (alloc_blocks_ != fs_->Info().alloc_block_count) {
        FS_TRACE_ERROR("check: incorrect allocated block count %u (should be %u)\n",
                       fs_->Info().alloc_block_count, alloc_blocks_);
        status = ZX_ERR_BAD_STATE;
    }

    if (alloc_inodes_ != fs_->Info().alloc_inode_count) {
        FS_TRACE_ERROR("check: incorrect allocated inode count %u (should be %u)\n",
                       fs_->Info().alloc_inode_count, alloc_inodes_);
        status = ZX_ERR_BAD_STATE;
    }

    return status;
}

MinfsChecker::MinfsChecker()
    : conforming_(true), fs_(nullptr), alloc_inodes_(0), alloc_blocks_(0), links_() {};

zx_status_t MinfsChecker::Init(fbl::unique_ptr<Bcache> bc, const minfs_info_t* info) {
    links_.reset(new int32_t[info->inode_count]{0}, info->inode_count);
    links_[0] = -1;

    cached_doubly_indirect_ = 0;
    cached_indirect_ = 0;

    zx_status_t status;
    if ((status = checked_inodes_.Reset(info->inode_count)) != ZX_OK) {
        FS_TRACE_ERROR("MinfsChecker::Init Failed to reset checked inodes: %d\n", status);
        return status;
    }
    if ((status = checked_blocks_.Reset(info->block_count)) != ZX_OK) {
        FS_TRACE_ERROR("MinfsChecker::Init Failed to reset checked blocks: %d\n", status);
        return status;
    }
    fbl::unique_ptr<Minfs> fs;
    if ((status = Minfs::Create(fbl::move(bc), info, &fs)) != ZX_OK) {
        FS_TRACE_ERROR("MinfsChecker::Create Failed to Create Minfs: %d\n", status);
        return status;
    }
    fs_ = fbl::move(fs);

    return ZX_OK;
}

zx_status_t minfs_check(fbl::unique_ptr<Bcache> bc) {
    zx_status_t status;

    char data[kMinfsBlockSize];
    if (bc->Readblk(0, data) < 0) {
        FS_TRACE_ERROR("minfs: could not read info block\n");
        return ZX_ERR_IO;
    }
    const minfs_info_t* info = reinterpret_cast<const minfs_info_t*>(data);
    minfs_dump_info(info);
    if ((status = minfs_check_info(info, bc.get())) != ZX_OK) {
        FS_TRACE_ERROR("minfs_check: check_info failure: %d\n", status);
        return status;
    }

    MinfsChecker chk;
    if ((status = chk.Init(fbl::move(bc), info)) != ZX_OK) {
        FS_TRACE_ERROR("minfs_check: Init failure: %d\n", status);
        return status;
    }

    chk.CheckReserved();

    //TODO: check root not a directory
    if ((status = chk.CheckInode(1, 1, 0)) != ZX_OK) {
        FS_TRACE_ERROR("minfs_check: CheckInode failure: %d\n", status);
        return status;
    }

    zx_status_t r;

    // Save an error if it occurs, but check for subsequent errors
    // anyway.
    r = chk.CheckForUnusedBlocks();
    status |= (status != ZX_OK) ? 0 : r;
    r = chk.CheckForUnusedInodes();
    status |= (status != ZX_OK) ? 0 : r;
    r = chk.CheckLinkCounts();
    status |= (status != ZX_OK) ? 0 : r;
    r = chk.CheckAllocatedCounts();
    status |= (status != ZX_OK) ? 0 : r;

    //TODO: check allocated inodes that were abandoned
    //TODO: check allocated blocks that were not accounted for
    //TODO: check unallocated inodes where magic != 0
    status |= (status != ZX_OK) ? 0 : (chk.conforming_ ? ZX_OK : ZX_ERR_BAD_STATE);

    return status;
}

} // namespace minfs
