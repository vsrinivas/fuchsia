// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <bitmap/raw-bitmap.h>
#include <fs/trace.h>
#include <mxalloc/new.h>
#include <mxtl/algorithm.h>
#include <mxtl/unique_ptr.h>

#include "minfs-private.h"
#include "writeback-queue.h"

namespace minfs {

void* GetNthBlock(const void* data, uint32_t blkno) {
    assert(kMinfsBlockSize <= (blkno + 1) * kMinfsBlockSize); // Avoid overflow
    return (void*)((uintptr_t)(data) + (uintptr_t)(kMinfsBlockSize * blkno));
}

void* GetBlock(const RawBitmap& bitmap, uint32_t blkno) {
    assert(blkno * kMinfsBlockSize < bitmap.size()); // Accessing beyond end of bitmap
    return GetNthBlock(bitmap.StorageUnsafe()->GetData(), blkno);
}

void* GetBitBlock(const RawBitmap& bitmap, uint32_t* blkno_out, uint32_t bitno) {
    assert(bitno <= bitmap.size());
    *blkno_out = (bitno / kMinfsBlockBits);
    return GetBlock(bitmap, *blkno_out);
}

void minfs_dump_info(minfs_info_t* info) {
    trace(MINFS, "minfs: blocks:  %10u (size %u)\n", info->block_count, info->block_size);
    trace(MINFS, "minfs: inodes:  %10u (size %u)\n", info->inode_count, info->inode_size);
    trace(MINFS, "minfs: inode bitmap @ %10u\n", info->ibm_block);
    trace(MINFS, "minfs: alloc bitmap @ %10u\n", info->abm_block);
    trace(MINFS, "minfs: inode table  @ %10u\n", info->ino_block);
    trace(MINFS, "minfs: data blocks  @ %10u\n", info->dat_block);
}

mx_status_t minfs_check_info(const minfs_info_t* info, uint32_t max) {
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

mx_status_t Minfs::InodeSync(uint32_t ino, const minfs_inode_t* inode) {
    // Obtain the offset of the inode within its containing block
    uint32_t off_of_ino = (ino % kMinfsInodesPerBlock) * kMinfsInodeSize;
#ifdef __Fuchsia__
    void* inodata = (void*)((uintptr_t)(inode_table_->GetData()) +
                            (uintptr_t)((ino / kMinfsInodesPerBlock) * kMinfsBlockSize));
#else
    uint8_t inodata[kMinfsBlockSize];
    bc_->Readblk(info_.ino_block + (ino / kMinfsInodesPerBlock), inodata);
#endif
    memcpy((void*)((uintptr_t)inodata + off_of_ino), inode, kMinfsInodeSize);

    // commit blocks to disk
    uint32_t bno_of_ino = info_.ino_block + (ino / kMinfsInodesPerBlock);
    bc_->Writeblk(bno_of_ino, inodata);
    return NO_ERROR;
}

Minfs::Minfs(Bcache* bc, const minfs_info_t* info) : bc_(bc) {
    memcpy(&info_, info, sizeof(minfs_info_t));
}

mx_status_t Minfs::InoFree(
#ifdef __Fuchsia__
                           const MappedVmo* vmo_indirect,
#endif
                           const minfs_inode_t& inode, uint32_t ino) {
    // locate data and block offset of bitmap
    void *bmdata;
    uint32_t ibm_relative_bno;
    if ((bmdata = GetBitBlock(inode_map_, &ibm_relative_bno, ino)) == nullptr) {
        panic("inode not in bitmap");
    }

    // update and commit block to disk
    inode_map_.Clear(ino, ino + 1);
    bc_->Writeblk(info_.ibm_block + ibm_relative_bno, bmdata);

    uint32_t block_count = inode.block_count;

    // We're going to be updating block bitmaps repeatedly.
    WritebackQueue<> txn(bc_, block_map_.StorageUnsafe()->GetData());

    // release all direct blocks
    for (unsigned n = 0; n < kMinfsDirect; n++) {
        if (inode.dnum[n] == 0) {
            continue;
        }
        ValidateBno(inode.dnum[n]);
        block_count--;
        block_map_.Clear(inode.dnum[n], inode.dnum[n] + 1);
        uint32_t bitblock = inode.dnum[n] / kMinfsBlockBits;
        txn.EnqueueDirty(bitblock, info_.abm_block + bitblock);
    }

    // release all indirect blocks
    for (unsigned n = 0; n < kMinfsIndirect; n++) {
        if (inode.inum[n] == 0) {
            continue;
        }
#ifdef __Fuchsia__
        uintptr_t iaddr = reinterpret_cast<uintptr_t>(vmo_indirect->GetData());
        uint32_t* entry = reinterpret_cast<uint32_t*>(iaddr + kMinfsBlockSize * n);
#else
        uint8_t idata[kMinfsBlockSize];
        bc_->Readblk(inode.inum[n], idata);
        uint32_t* entry = reinterpret_cast<uint32_t*>(idata);
#endif
        // release the blocks pointed at by the entries in the indirect block
        for (unsigned m = 0; m < (kMinfsBlockSize / sizeof(uint32_t)); m++) {
            if (entry[m] == 0) {
                continue;
            }
            block_count--;
            block_map_.Clear(entry[m], entry[m] + 1);
            uint32_t bitblock = entry[m] / kMinfsBlockBits;
            txn.EnqueueDirty(bitblock, info_.abm_block + bitblock);
        }
        // release the direct block itself
        block_count--;
        block_map_.Clear(inode.inum[n], inode.inum[n] + 1);
        uint32_t bitblock = inode.inum[n] / kMinfsBlockBits;
        txn.EnqueueDirty(bitblock, info_.abm_block + bitblock);
    }

    MX_DEBUG_ASSERT(block_count == 0);
    return NO_ERROR;
}

mx_status_t Minfs::InoNew(const minfs_inode_t* inode, uint32_t* ino_out) {
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
    uint32_t ibm_relative_bno;
    if ((bmdata = GetBitBlock(inode_map_, &ibm_relative_bno, ino)) == nullptr) {
        panic("inode not in bitmap");
    }

    // TODO(smklein): optional sanity check of both blocks

    // Write the inode back first
    if ((status = InodeSync(ino, inode)) != NO_ERROR) {
        inode_map_.Clear(ino, ino + 1);
        return status;
    }

    // commit blocks to disk
    bc_->Writeblk(info_.ibm_block + ibm_relative_bno, bmdata);

    *ino_out = ino;
    return NO_ERROR;
}

mx_status_t Minfs::VnodeNew(mxtl::RefPtr<VnodeMinfs>* out, uint32_t type) {
    if ((type != kMinfsTypeFile) && (type != kMinfsTypeDir)) {
        return ERR_INVALID_ARGS;
    }

    mxtl::RefPtr<VnodeMinfs> vn;
    mx_status_t status;

    // Allocate the in-memory vnode
    if ((status = VnodeMinfs::Allocate(this, type, &vn)) != NO_ERROR) {
        return status;
    }

    // Allocate the on-disk inode
    if ((status = InoNew(&vn->inode_, &vn->ino_)) != NO_ERROR) {
        return status;
    }

    vnode_hash_.insert(vn.get());

    *out = mxtl::move(vn);
    return 0;
}

void Minfs::VnodeRelease(VnodeMinfs* vn) {
    vnode_hash_.erase(*vn);
}

mx_status_t Minfs::VnodeGet(mxtl::RefPtr<VnodeMinfs>* out, uint32_t ino) {
    if ((ino < 1) || (ino >= info_.inode_count)) {
        return ERR_OUT_OF_RANGE;
    }
    mxtl::RefPtr<VnodeMinfs> vn = mxtl::RefPtr<VnodeMinfs>(vnode_hash_.find(ino).CopyPointer());
    if (vn != nullptr) {
        *out = mxtl::move(vn);
        return NO_ERROR;
    }
    mx_status_t status;
    if ((status = VnodeMinfs::AllocateHollow(this, &vn)) != NO_ERROR) {
        return ERR_NO_MEMORY;
    }

    // obtain the block of the inode table we need
    uint32_t off_of_ino = (ino % kMinfsInodesPerBlock) * kMinfsInodeSize;
#ifdef __Fuchsia__
    void* inodata = (void*)((uintptr_t)(inode_table_->GetData()) +
                            (uintptr_t)((ino / kMinfsInodesPerBlock) * kMinfsBlockSize));
#else
    uint8_t inodata[kMinfsBlockSize];
    bc_->Readblk(info_.ino_block + (ino / kMinfsInodesPerBlock), inodata);
#endif
    memcpy(&vn->inode_, (void*)((uintptr_t)inodata + off_of_ino), kMinfsInodeSize);

    vn->fs_ = this;
    vn->ino_ = ino;
    vnode_hash_.insert(vn.get());

    *out = mxtl::move(vn);
    return NO_ERROR;
}

// Allocate a new data block from the block bitmap.
//
// If hint is nonzero it indicates which block number to start the search for
// free blocks from.
mx_status_t Minfs::BlockNew(uint32_t hint, uint32_t* out_bno) {
    size_t bitoff_start;
    mx_status_t status;
    if ((status = block_map_.Find(false, hint, block_map_.size(), 1, &bitoff_start)) != NO_ERROR) {
        if ((status = block_map_.Find(false, 0, hint, 1, &bitoff_start)) != NO_ERROR) {
            return ERR_NO_SPACE;
        }
    }

    status = block_map_.Set(bitoff_start, bitoff_start + 1);
    assert(status == NO_ERROR);
    uint32_t bno = static_cast<uint32_t>(bitoff_start);
    ValidateBno(bno);

    // obtain the in-memory bitmap block
    uint32_t bmbno;
    void *bmdata = GetBitBlock(block_map_, &bmbno, bno); // bmbno relative to bitmap
    bmbno += info_.abm_block;                            // bmbno relative to block device

    // commit the bitmap
    bc_->Writeblk(bmbno, bmdata);
    ValidateBno(bno);
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

#ifdef __Fuchsia__
static const unsigned kPoolSize = 4;
#endif

mx_status_t Minfs::Create(Minfs** out, Bcache* bc, const minfs_info_t* info) {
    uint32_t blocks = bc->Maxblk();
    uint32_t inodes = info->inode_count;

    mx_status_t status = minfs_check_info(info, blocks);
    if (status < 0) {
        return status;
    }

    AllocChecker ac;
    mxtl::unique_ptr<Minfs> fs(new (&ac) Minfs(bc, info));
    if (!ac.check()) {
        return ERR_NO_MEMORY;
    }
#ifdef __Fuchsia__
    mxtl::unique_ptr<fs::VfsDispatcher> dispatcher(new (&ac) fs::VfsDispatcher());
    if (!ac.check()) {
        return ERR_NO_MEMORY;
    }

    status = dispatcher->Create(mxrio_handler, kPoolSize);
    if (status != NO_ERROR) {
        return status;
    }

    status = dispatcher->Start("Minfs Dispatcher");
    if (status != NO_ERROR) {
        return status;
    }
    fs->dispatcher_.swap(dispatcher);
#endif
    // determine how many blocks of inodes, allocation bitmaps,
    // and inode bitmaps there are
    fs->abmblks_ = (blocks + kMinfsBlockBits - 1) / kMinfsBlockBits;
    fs->ibmblks_ = (inodes + kMinfsBlockBits - 1) / kMinfsBlockBits;

    if ((status = fs->block_map_.Reset(fs->abmblks_ * kMinfsBlockBits)) < 0) {
        return status;
    }
    if ((status = fs->inode_map_.Reset(fs->ibmblks_ * kMinfsBlockBits)) < 0) {
        return status;
    }
    // this keeps the underlying storage a block multiple but ensures we
    // can't allocate beyond the last real block or inode
    if ((status = fs->block_map_.Shrink(fs->info_.block_count)) < 0) {
        return status;
    }
    if ((status = fs->inode_map_.Shrink(fs->info_.inode_count)) < 0) {
        return status;
    }

    if ((status = fs->LoadBitmaps()) < 0) {
        return status;
    }

#ifdef __Fuchsia__
    // Create the inode table.
    // TODO(smklein): Don't bother reading the ENTIRE thing into memory when we
    // can page in parts of it on-demand.
    uint32_t inoblks = (inodes + kMinfsInodesPerBlock - 1) / kMinfsInodesPerBlock;
    if ((status = MappedVmo::Create(inoblks * kMinfsBlockSize, &fs->inode_table_)) != NO_ERROR) {
        return status;
    }

    for (uint32_t n = 0; n < inoblks; n++) {
        void* bmdata = (void*)((uintptr_t)(fs->inode_table_->GetData()) +
                               (uintptr_t)(kMinfsBlockSize * n));
        if (fs->bc_->Readblk(fs->info_.ino_block + n, bmdata)) {
            error("minfs: failed reading inode table\n");
        }
    }
#endif

    *out = fs.release();
    return NO_ERROR;
}

mx_status_t Minfs::LoadBitmaps() {
    for (uint32_t n = 0; n < abmblks_; n++) {
        void* bmdata = GetBlock(block_map_, n);
        if (bc_->Readblk(info_.abm_block + n, bmdata)) {
            error("minfs: failed reading alloc bitmap\n");
        }
    }
    for (uint32_t n = 0; n < ibmblks_; n++) {
        void* bmdata = GetBlock(inode_map_, n);
        if (bc_->Readblk(info_.ibm_block + n, bmdata)) {
            error("minfs: failed reading inode bitmap\n");
        }
    }
    return NO_ERROR;
}

mx_status_t minfs_mount(mxtl::RefPtr<VnodeMinfs>* out, Bcache* bc) {
    minfs_info_t info;
    mx_status_t status;

    if ((status = bc->Read(0, &info, 0, sizeof(info))) != NO_ERROR) {
        error("minfs: could not read info block\n");
        return status;
    }

    Minfs* fs;
    if ((status = Minfs::Create(&fs, bc, &info)) != NO_ERROR) {
        error("minfs: mount failed\n");
        return status;
    }

    mxtl::RefPtr<VnodeMinfs> vn;
    if ((status = fs->VnodeGet(&vn, kMinfsRootIno)) != NO_ERROR) {
        error("minfs: cannot find root inode\n");
        delete fs;
        return status;
    }

    *out = mxtl::move(vn);
    return NO_ERROR;
}

mx_status_t Minfs::Unmount() {
#ifdef __Fuchsia__
    dispatcher_ = nullptr;
#endif
    return bc_->Close();
}

#ifdef __Fuchsia__
mx_status_t VnodeMinfs::AddDispatcher(mx_handle_t h, vfs_iostate_t* cookie) {
    return fs_->AddDispatcher(h, cookie);
}

mx_status_t Minfs::AddDispatcher(mx_handle_t h, vfs_iostate_t* cookie) {
    return dispatcher_->Add(h, reinterpret_cast<void*>(vfs_handler), cookie);
}
#endif

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
    // For now, we are aligning the
    //  - Inode bitmap
    //  - Block bitmap
    //  - Inode table
    // To an 8-block boundary on disk, allowing for future expansion.
    info.ibm_block = 8;
    info.abm_block = info.ibm_block + mxtl::roundup(ibmblks, 8u);
    info.ino_block = info.abm_block + mxtl::roundup(abmblks, 8u);
    info.dat_block = info.ino_block + inoblks;
    minfs_dump_info(&info);

    RawBitmap abm;
    RawBitmap ibm;

    // By allocating the bitmap and then shrinking it, we keep the underlying
    // storage a block multiple but ensure we can't allocate beyond the last
    // real block or inode.
    mx_status_t status;
    if ((status = abm.Reset(mxtl::roundup(info.block_count, kMinfsBlockBits))) < 0) {
        error("mkfs: Failed to allocate block bitmap\n");
        return status;
    }
    if ((status = ibm.Reset(mxtl::roundup(info.inode_count, kMinfsBlockBits))) < 0) {
        error("mkfs: Failed to allocate inode bitmap\n");
        return status;
    }
    if ((status = abm.Shrink(info.block_count)) < 0) {
        error("mkfs: Failed to shrink block bitmap\n");
        return status;
    }
    if ((status = ibm.Shrink(info.inode_count)) < 0) {
        error("mkfs: Failed to shrink inode bitmap\n");
        return status;
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

} // namespace minfs
