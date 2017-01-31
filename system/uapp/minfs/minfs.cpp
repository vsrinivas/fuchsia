// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <bitmap/raw-bitmap.h>
#include <mxtl/unique_ptr.h>

#include "minfs-private.h"

void* GetBlock(const bitmap::RawBitmap& bitmap, uint32_t blkno) {
    assert(blkno * kMinfsBlockSize <= bitmap.size());
    return (void*)((uintptr_t)(bitmap.data_unsafe()) + (uintptr_t)(kMinfsBlockSize * blkno));
}

void* GetBitBlock(const bitmap::RawBitmap& bitmap, uint32_t* blkno_out, uint32_t bitno) {
    assert(bitno <= bitmap.size());
    *blkno_out = (bitno / kMinfsBlockBits);
    return GetBlock(bitmap, *blkno_out);
}

void minfs_dump_info(minfs_info_t* info) {
    printf("minfs: blocks:  %10u (size %u)\n", info->block_count, info->block_size);
    printf("minfs: inodes:  %10u (size %u)\n", info->inode_count, info->inode_size);
    printf("minfs: inode bitmap @ %10u\n", info->ibm_block);
    printf("minfs: alloc bitmap @ %10u\n", info->abm_block);
    printf("minfs: inode table  @ %10u\n", info->ino_block);
    printf("minfs: data blocks  @ %10u\n", info->dat_block);
}

static mx_time_t minfs_gettime_utc() {
    // linux/magenta compatible
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    mx_time_t time = MX_SEC(ts.tv_sec)+ts.tv_nsec;
    return time;
}

mx_status_t minfs_check_info(minfs_info_t* info, uint32_t max) {
    if ((info->magic0 != kMinfsMagic0) ||
        (info->magic1 != kMinfsMagic1)) {
        error("minfs: bad magic\n");
        return ERR_INVALID_ARGS;
    }
    if (info->version != kMinfsVersion) {
        error("minfs: FS Version: %08x. Driver version: %08x\n", info->version,
              kMinfsVersion);
        return ERR_INVALID_ARGS;
    }
    if ((info->block_size != kMinfsBlockSize) ||
        (info->inode_size != kMinfsInodeSize)) {
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

// sync the data portion of the current vnode
void minfs_sync_vnode(vnode_t* vn, uint32_t flags) {

    // by default, c/mtimes are not updated to current time
    if (flags != kMxFsSyncDefault) {
        mx_time_t cur_time = minfs_gettime_utc();
        // update times before syncing
        if ((flags & kMxFsSyncMtime) != 0) {
            vn->inode.modify_time = cur_time;
        }
        if ((flags & kMxFsSyncCtime) != 0) {
            vn->inode.create_time = cur_time;
        }
        // TODO(orr): no current support for atime
    }

    uint32_t bno_of_ino = vn->fs->info.ino_block + (vn->ino / kMinfsInodesPerBlock);
    uint32_t off_of_ino = (vn->ino % kMinfsInodesPerBlock) * kMinfsInodeSize;

    mxtl::RefPtr<BlockNode> blk;
    if ((blk = vn->fs->bc->Get(bno_of_ino)) == nullptr) {
        panic("failed sync vnode %p(#%u)", vn, vn->ino);
    }

    memcpy((void*)((uintptr_t)blk->data() + off_of_ino), &vn->inode, kMinfsInodeSize);
    vn->fs->bc->Put(blk, kBlockDirty);
}

Minfs::Minfs(Bcache* bc_, minfs_info_t* info_) : bc(bc_) {
    memcpy(&info, info_, sizeof(minfs_info_t));
    for (size_t n = 0; n < kMinfsBuckets; n++) {
        list_initialize(vnode_hash_ + n);
    }
}

mx_status_t Minfs::InoFree(const minfs_inode_t& inode, uint32_t ino) {
    // locate data and block offset of bitmap
    void *bmdata;
    uint32_t bmbno;
    if ((bmdata = GetBitBlock(inode_map_, &bmbno, ino)) == nullptr) {
        panic("inode not in bitmap");
    }

    // obtain the block of the inode bitmap we need
    mxtl::RefPtr<BlockNode> block_ibm;
    if ((block_ibm = bc->Get(info.ibm_block + bmbno)) == nullptr) {
        return ERR_IO;
    }

    // update and commit block to disk
    inode_map_.Clear(ino, ino + 1);
    memcpy(block_ibm->data(), bmdata, kMinfsBlockSize);
    bc->Put(block_ibm, kBlockDirty);

    mxtl::RefPtr<BlockNode> bitmap_blk;

    // release all direct blocks
    for (unsigned n = 0; n < kMinfsDirect; n++) {
        if (inode.dnum[n] == 0) {
            continue;
        }
        if ((bitmap_blk = BitmapBlockGet(bitmap_blk, inode.dnum[n])) == nullptr) {
            return ERR_IO;
        }
        block_map.Clear(inode.dnum[n], inode.dnum[n] + 1);
    }

    // release all indirect blocks
    for (unsigned n = 0; n < kMinfsIndirect; n++) {
        if (inode.inum[n] == 0) {
            continue;
        }
        mxtl::RefPtr<BlockNode> blk;
        if ((blk = bc->Get(inode.inum[n])) == nullptr) {
            BitmapBlockPut(bitmap_blk);
            return ERR_IO;
        }
        uint32_t* entry = static_cast<uint32_t*>(blk->data());
        // release the blocks pointed at by the entries in the indirect block
        for (unsigned m = 0; m < (kMinfsBlockSize / sizeof(uint32_t)); m++) {
            if (entry[m] == 0) {
                continue;
            }
            if ((bitmap_blk = BitmapBlockGet(bitmap_blk, entry[m])) == nullptr) {
                bc->Put(blk, 0);
                return ERR_IO;
            }
            block_map.Clear(entry[m], entry[m] + 1);
        }
        bc->Put(blk, 0);
        // release the direct block itself
        if ((bitmap_blk = BitmapBlockGet(bitmap_blk, inode.inum[n])) == nullptr) {
            return ERR_IO;
        }
        block_map.Clear(inode.inum[n], inode.inum[n] + 1);
    }
    BitmapBlockPut(bitmap_blk);

    return NO_ERROR;
}

mx_status_t Minfs::InoNew(minfs_inode_t* inode, uint32_t* ino_out) {
    size_t bitoff_start;
    mx_status_t status = inode_map_.Find(false, 0, inode_map_.size(), 1, &bitoff_start);
    if (status != NO_ERROR) {
        return status;
    }
    status = inode_map_.Set(bitoff_start, bitoff_start + 1);
    assert(status == NO_ERROR);
    uint32_t ino = static_cast<uint32_t>(bitoff_start);

    // locate data and block offset of bitmap
    void *bmdata;
    uint32_t bmbno;
    if ((bmdata = GetBitBlock(inode_map_, &bmbno, ino)) == nullptr) {
        panic("inode not in bitmap");
    }

    // obtain the block of the inode bitmap we need
    mxtl::RefPtr<BlockNode> block_ibm;
    if ((block_ibm = bc->Get(info.ibm_block + bmbno)) == nullptr) {
        inode_map_.Clear(ino, ino + 1);
        return ERR_IO;
    }

    uint32_t bno_of_ino = info.ino_block + (ino / kMinfsInodesPerBlock);
    uint32_t off_of_ino = (ino % kMinfsInodesPerBlock) * kMinfsInodeSize;

    // obtain the block of the inode table we need
    mxtl::RefPtr<BlockNode> block_ino;
    if ((block_ino = bc->Get(bno_of_ino)) == nullptr) {
        inode_map_.Clear(ino, ino + 1);
        bc->Put(block_ibm, 0);
        return ERR_IO;
    }

    //TODO: optional sanity check of both blocks

    // write data to blocks in memory
    memcpy(block_ibm->data(), bmdata, kMinfsBlockSize);
    memcpy((void*)((uintptr_t)block_ino->data() + off_of_ino), inode, kMinfsInodeSize);

    // commit blocks to disk
    bc->Put(block_ibm, kBlockDirty);
    bc->Put(block_ino, kBlockDirty);

    *ino_out = ino;
    return NO_ERROR;
}

mx_status_t Minfs::VnodeNew(vnode_t** out, uint32_t type) {
    vnode_t* vn;
    if ((type != kMinfsTypeFile) && (type != kMinfsTypeDir)) {
        return ERR_INVALID_ARGS;
    }
    if ((vn = (vnode_t*)calloc(1, sizeof(vnode_t))) == nullptr) {
        return ERR_NO_MEMORY;
    }
    vn->inode.magic = MinfsMagic(type);
    vn->inode.create_time = vn->inode.modify_time = minfs_gettime_utc();
    vn->inode.link_count = 1;
    vn->refcount = 1;
    vn->ops = &minfs_ops;
    mx_status_t status;
    if ((status = InoNew(&vn->inode, &vn->ino)) != NO_ERROR) {
        free(vn);
        return status;
    }
    vn->fs = this;
    list_add_tail(vnode_hash_ + INO_HASH(vn->ino), &vn->hashnode);

    trace(MINFS, "new_vnode() %p(#%u) { magic=%#08x }\n",
          vn, vn->ino, vn->inode.magic);

    *out = vn;
    return 0;
}

mx_status_t Minfs::VnodeGet(vnode_t** out, uint32_t ino) {
    if ((ino < 1) || (ino >= info.inode_count)) {
        return ERR_OUT_OF_RANGE;
    }
    vnode_t* vn;
    uint32_t bucket = INO_HASH(ino);
    list_for_every_entry(vnode_hash_ + bucket, vn, vnode_t, hashnode) {
        if (vn->ino == ino) {
            vn_acquire(vn);
            *out = vn;
            return NO_ERROR;
        }
    }
    if ((vn = (vnode_t*)calloc(1, sizeof(vnode_t))) == nullptr) {
        return ERR_NO_MEMORY;
    }
    mx_status_t status;
    uint32_t ino_per_blk = info.block_size / kMinfsInodeSize;
    if ((status = bc->Read(info.ino_block + ino / ino_per_blk, &vn->inode,
                           kMinfsInodeSize * (ino % ino_per_blk), kMinfsInodeSize)) < 0) {
        return status;
    }
    trace(MINFS, "get_vnode() %p(#%u) { magic=%#08x size=%u blks=%u dn=%u,%u,%u,%u... }\n",
          vn, ino, vn->inode.magic, vn->inode.size, vn->inode.block_count,
          vn->inode.dnum[0], vn->inode.dnum[1], vn->inode.dnum[2],
          vn->inode.dnum[3]);
    vn->fs = this;
    vn->ino = ino;
    vn->refcount = 1;
    vn->ops = &minfs_ops;
    list_add_tail(vnode_hash_ + bucket, &vn->hashnode);

    *out = vn;
    return NO_ERROR;
}

// Allocate a new data block from the block bitmap.
// Return the underlying block (obtained via Bcache::Get()), if 'out_block' is not nullptr.
//
// If hint is nonzero it indicates which block number to start the search for
// free blocks from.
mx_status_t Minfs::BlockNew(uint32_t hint, uint32_t* out_bno, mxtl::RefPtr<BlockNode> *out_block) {
    size_t bitoff_start;
    mx_status_t status;
    if ((status = block_map.Find(false, hint, block_map.size(), 1, &bitoff_start)) != NO_ERROR) {
        if ((status = block_map.Find(false, 0, hint, 1, &bitoff_start)) != NO_ERROR) {
            return ERR_NO_SPACE;
        }
    }
    status = block_map.Set(bitoff_start, bitoff_start + 1);
    assert(status == NO_ERROR);
    uint32_t bno = static_cast<uint32_t>(bitoff_start);
    assert(bno != 0); // Cannot allocate root block

    // obtain the in-memory bitmap block
    uint32_t bmbno;
    void *bmdata = GetBitBlock(block_map, &bmbno, bno); // bmbno relative to bitmap
    bmbno += info.abm_block;                            // bmbno relative to block device

    // obtain the block of the alloc bitmap we need
    mxtl::RefPtr<BlockNode> block_abm;
    if ((block_abm = bc->Get(bmbno)) == nullptr) {
        block_map.Clear(bno, bno + 1);
        return ERR_IO;
    }

    // obtain the block we're allocating, if requested.
    if (out_block != nullptr) {
        if ((*out_block = bc->GetZero(bno)) == nullptr) {
            block_map.Clear(bno, bno + 1);
            bc->Put(block_abm, 0);
            return ERR_IO;
        }
    }

    // commit the bitmap
    memcpy(block_abm->data(), bmdata, kMinfsBlockSize);
    bc->Put(block_abm, kBlockDirty);
    *out_bno = bno;
    return NO_ERROR;
}

void minfs_dir_init(void* bdata, uint32_t ino_self, uint32_t ino_parent) {
#define DE0_SIZE DirentSize(1)

    // directory entry for self
    minfs_dirent_t* de = (minfs_dirent_t*) bdata;
    de->ino = ino_self;
    de->reclen = DE0_SIZE;
    de->namelen = 1;
    de->type = kMinfsTypeDir;
    de->name[0] = '.';

    // directory entry for parent
    de = (minfs_dirent_t*)((uintptr_t)bdata + DE0_SIZE);
    de->ino = ino_parent;
    de->reclen = DirentSize(2) | kMinfsReclenLast;
    de->namelen = 2;
    de->type = kMinfsTypeDir;
    de->name[0] = '.';
    de->name[1] = '.';
}

mx_status_t Minfs::Create(Minfs** out, Bcache* bc, minfs_info_t* info) {
    uint32_t blocks = bc->Maxblk();
    uint32_t inodes = info->inode_count;

    mx_status_t status = minfs_check_info(info, blocks);
    if (status < 0) {
        return status;
    }

    mxtl::unique_ptr<Minfs> fs(new Minfs(bc, info));
    if (fs == nullptr) {
        return ERR_NO_MEMORY;
    }

    // determine how many blocks of inodes, allocation bitmaps,
    // and inode bitmaps there are
    //uint32_t inoblks = (inodes + kMinfsInodesPerBlock - 1) / kMinfsInodesPerBlock;
    fs->abmblks_ = (blocks + kMinfsBlockBits - 1) / kMinfsBlockBits;
    fs->ibmblks_ = (inodes + kMinfsBlockBits - 1) / kMinfsBlockBits;

    if ((status = fs->block_map.Reset(fs->abmblks_ * kMinfsBlockBits)) < 0) {
        return status;
    }
    if ((status = fs->inode_map_.Reset(fs->ibmblks_ * kMinfsBlockBits)) < 0) {
        return status;
    }
    // this keeps the underlying storage a block multiple but ensures we
    // can't allocate beyond the last real block or inode
    if ((status = fs->block_map.Shrink(fs->info.block_count)) < 0) {
        return status;
    }
    if ((status = fs->inode_map_.Shrink(fs->info.inode_count)) < 0) {
        return status;
    }

    if ((status = fs->LoadBitmaps()) < 0) {
        return status;
    }
    *out = fs.release();
    return NO_ERROR;
}

mx_status_t Minfs::LoadBitmaps() {
    for (uint32_t n = 0; n < abmblks_; n++) {
        void* bmdata = GetBlock(block_map, n);
        if (bc->Read(info.abm_block + n, bmdata, 0, kMinfsBlockSize)) {
            error("minfs: failed reading alloc bitmap\n");
        }
    }
    for (uint32_t n = 0; n < ibmblks_; n++) {
        void* bmdata = GetBlock(inode_map_, n);
        if (bc->Read(info.ibm_block + n, bmdata, 0, kMinfsBlockSize)) {
            error("minfs: failed reading inode bitmap\n");
        }
    }
    return NO_ERROR;
}

mx_status_t minfs_mount(vnode_t** out, Bcache* bc) {
    minfs_info_t info;

    if (bc->Read(0, &info, 0, sizeof(info)) < 0) {
        error("minfs: could not read info block\n");
        return -1;
    }
    if (minfs_check_info(&info, bc->Maxblk())) {
        return -1;
    }

    Minfs* fs;
    if (Minfs::Create(&fs, bc, &info)) {
        error("minfs: mount failed\n");
        return -1;
    }

    vnode_t* vn;
    if (fs->VnodeGet(&vn, kMinfsRootIno)) {
        error("minfs: cannot find root inode\n");
        delete fs;
        return -1;
    }

    *out = vn;
    return NO_ERROR;
}

mx_status_t Minfs::Unmount() {
    return bc->Close();
}

int minfs_mkfs(Bcache* bc) {
    uint32_t blocks = bc->Maxblk();
    uint32_t inodes = 32768;

    // determine how many blocks of inodes, allocation bitmaps,
    // and inode bitmaps there are
    uint32_t inoblks = (inodes + kMinfsInodesPerBlock - 1) / kMinfsInodesPerBlock;
    uint32_t abmblks = (blocks + kMinfsBlockBits - 1) / kMinfsBlockBits;
    uint32_t ibmblks = (inodes + kMinfsBlockBits - 1) / kMinfsBlockBits;

    minfs_info_t info;
    memset(&info, 0x00, sizeof(info));
    info.magic0 = kMinfsMagic0;
    info.magic1 = kMinfsMagic1;
    info.version = kMinfsVersion;
    info.flags = kMinfsFlagClean;
    info.block_size = kMinfsBlockSize;
    info.inode_size = kMinfsInodeSize;
    info.block_count = blocks;
    info.inode_count = inodes;
    info.ibm_block = 8;
    info.abm_block = 16;
    info.ino_block = info.abm_block + ((abmblks + 8) & (~7));
    info.dat_block = info.ino_block + inoblks;
    minfs_dump_info(&info);

    bitmap::RawBitmap abm;
    bitmap::RawBitmap ibm;
    if (abm.Reset(info.block_count)) {
        return -1;
    }
    if (ibm.Reset(info.inode_count)) {
        return -1;
    }

    // write rootdir
    mxtl::RefPtr<BlockNode> blk = bc->GetZero(info.dat_block);
    if (blk == nullptr) {
        error("mkfs: Insufficient space\n");
        return -1;
    }
    minfs_dir_init(blk->data(), kMinfsRootIno, kMinfsRootIno);
    bc->Put(blk, kBlockDirty);

    // update inode bitmap
    ibm.Set(0, 1);
    ibm.Set(kMinfsRootIno, kMinfsRootIno + 1);

    // update block bitmap:
    // reserve all blocks before the data storage area
    // reserve the first data block (for root directory)
    abm.Set(0, info.dat_block + 1);

    // write allocation bitmap
    for (uint32_t n = 0; n < abmblks; n++) {
        void* bmdata = GetBlock(abm, n);
        blk = bc->GetZero(info.abm_block + n);
        memcpy(blk->data(), bmdata, kMinfsBlockSize);
        bc->Put(blk, kBlockDirty);
    }

    // write inode bitmap
    for (uint32_t n = 0; n < ibmblks; n++) {
        void* bmdata = GetBlock(ibm, n);
        blk = bc->GetZero(info.ibm_block + n);
        memcpy(blk->data(), bmdata, kMinfsBlockSize);
        bc->Put(blk, kBlockDirty);
    }

    // write inodes
    for (uint32_t n = 0; n < inoblks; n++) {
        blk = bc->GetZero(info.ino_block + n);
        bc->Put(blk, kBlockDirty);
    }


    // setup root inode
    blk = bc->Get(info.ino_block);
    minfs_inode_t* ino = (minfs_inode_t*) blk->data();
    ino[kMinfsRootIno].magic = kMinfsMagicDir;
    ino[kMinfsRootIno].size = kMinfsBlockSize;
    ino[kMinfsRootIno].block_count = 1;
    ino[kMinfsRootIno].link_count = 1;
    ino[kMinfsRootIno].dirent_count = 2;
    ino[kMinfsRootIno].dnum[0] = info.dat_block;
    bc->Put(blk, kBlockDirty);

    blk = bc->GetZero(0);
    memcpy(blk->data(), &info, sizeof(info));
    bc->Put(blk, kBlockDirty);
    return 0;
}
