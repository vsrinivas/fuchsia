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
#include <fs/block-txn.h>
#include <fs/trace.h>
#include <mxalloc/new.h>
#include <mxtl/algorithm.h>
#include <mxtl/unique_ptr.h>
#ifdef __Fuchsia__
#include <fs/vfs-dispatcher.h>
#endif

#include "minfs-private.h"

namespace minfs {

void minfs_dump_info(const minfs_info_t* info) {
    FS_TRACE(MINFS, "minfs: blocks:  %10u (size %u)\n", info->block_count, info->block_size);
    FS_TRACE(MINFS, "minfs: inodes:  %10u (size %u)\n", info->inode_count, info->inode_size);
    FS_TRACE(MINFS, "minfs: allocated blocks  @ %10u\n", info->alloc_block_count);
    FS_TRACE(MINFS, "minfs: allocated inodes  @ %10u\n", info->alloc_inode_count);
    FS_TRACE(MINFS, "minfs: inode bitmap @ %10u\n", info->ibm_block);
    FS_TRACE(MINFS, "minfs: alloc bitmap @ %10u\n", info->abm_block);
    FS_TRACE(MINFS, "minfs: inode table  @ %10u\n", info->ino_block);
    FS_TRACE(MINFS, "minfs: data blocks  @ %10u\n", info->dat_block);
}

void minfs_dump_inode(const minfs_inode_t* inode, uint32_t ino) {
    FS_TRACE(MINFS, "inode[%u]: magic:  %10u\n", ino, inode->magic);
    FS_TRACE(MINFS, "inode[%u]: size:   %10u\n", ino, inode->size);
    FS_TRACE(MINFS, "inode[%u]: blocks: %10u\n", ino, inode->block_count);
    FS_TRACE(MINFS, "inode[%u]: links:  %10u\n", ino, inode->link_count);
}

mx_status_t minfs_check_info(const minfs_info_t* info, uint32_t max) {
    minfs_dump_info(info);

    if ((info->magic0 != kMinfsMagic0) ||
        (info->magic1 != kMinfsMagic1)) {
        FS_TRACE_ERROR("minfs: bad magic\n");
        return MX_ERR_INVALID_ARGS;
    }
    if (info->version != kMinfsVersion) {
        FS_TRACE_ERROR("minfs: FS Version: %08x. Driver version: %08x\n", info->version,
              kMinfsVersion);
        return MX_ERR_INVALID_ARGS;
    }
    if ((info->block_size != kMinfsBlockSize) ||
        (info->inode_size != kMinfsInodeSize)) {
        FS_TRACE_ERROR("minfs: bsz/isz %u/%u unsupported\n", info->block_size, info->inode_size);
        return MX_ERR_INVALID_ARGS;
    }
    if (info->block_count > max) {
        FS_TRACE_ERROR("minfs: too large for device\n");
        return MX_ERR_INVALID_ARGS;
    }
    //TODO: validate layout
    return 0;
}

mx_status_t Minfs::InodeSync(WriteTxn* txn, uint32_t ino, const minfs_inode_t* inode) {
    // Obtain the offset of the inode within its containing block
    const uint32_t off_of_ino = (ino % kMinfsInodesPerBlock) * kMinfsInodeSize;
    const uint32_t inoblock_rel = ino / kMinfsInodesPerBlock;
    const uint32_t inoblock_abs = inoblock_rel + info_.ino_block;
#ifdef __Fuchsia__
    void* inodata = (void*)((uintptr_t)(inode_table_->GetData()) +
                            (uintptr_t)(inoblock_rel * kMinfsBlockSize));
    auto itable_id = inode_table_vmoid_;
    memcpy((void*)((uintptr_t)inodata + off_of_ino), inode, kMinfsInodeSize);
    txn->Enqueue(itable_id, inoblock_rel, inoblock_abs, 1);
#else
    // Since host-side tools don't have "mapped vmos", just read / update /
    // write the single absolute indoe block.
    uint8_t inodata[kMinfsBlockSize];
    bc_->Readblk(inoblock_abs, inodata);
    memcpy((void*)((uintptr_t)inodata + off_of_ino), inode, kMinfsInodeSize);
    bc_->Writeblk(inoblock_abs, inodata);
#endif
    return MX_OK;
}

Minfs::Minfs(mxtl::unique_ptr<Bcache> bc, const minfs_info_t* info) : bc_(mxtl::move(bc)) {
    memcpy(&info_, info, sizeof(minfs_info_t));
}

Minfs::~Minfs() {
    vnode_hash_.clear();
}

mx_status_t Minfs::InoFree(
#ifdef __Fuchsia__
    const MappedVmo* vmo_indirect,
#endif
    const minfs_inode_t& inode, uint32_t ino) {
    // We're going to be updating block bitmaps repeatedly.
    WriteTxn txn(bc_.get());
#ifdef __Fuchsia__
    auto ibm_id = inode_map_vmoid_;
#else
    auto ibm_id = inode_map_.StorageUnsafe()->GetData();
#endif

    // Free the inode bit itself
    inode_map_.Clear(ino, ino + 1);
    info_.alloc_inode_count--;

    uint32_t bitblock = ino / kMinfsBlockBits;
    txn.Enqueue(ibm_id, bitblock, info_.ibm_block + bitblock, 1);
    uint32_t block_count = inode.block_count;

    // release all direct blocks
    for (unsigned n = 0; n < kMinfsDirect; n++) {
        if (inode.dnum[n] == 0) {
            continue;
        }
        ValidateBno(inode.dnum[n]);
        block_count--;
        BlockFree(&txn, inode.dnum[n]);
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
            BlockFree(&txn, entry[m]);
        }
        // release the direct block itself
        block_count--;
        BlockFree(&txn, inode.inum[n]);
    }

    CountUpdate(&txn);
    MX_DEBUG_ASSERT(block_count == 0);
    return MX_OK;
}

mx_status_t Minfs::InoNew(WriteTxn* txn, const minfs_inode_t* inode, uint32_t* ino_out) {
    size_t bitoff_start;
    mx_status_t status = inode_map_.Find(false, 0, inode_map_.size(), 1, &bitoff_start);
    if (status != MX_OK) {
        return status;
    }

    status = inode_map_.Set(bitoff_start, bitoff_start + 1);
    assert(status == MX_OK);
    info_.alloc_inode_count++;
    uint32_t ino = static_cast<uint32_t>(bitoff_start);

    // locate data and block offset of bitmap
    void* bmdata;
    MX_DEBUG_ASSERT(ino <= inode_map_.size());
    uint32_t ibm_relative_bno = (ino / kMinfsBlockBits);
    if ((bmdata = fs::GetBlock<kMinfsBlockSize>(inode_map_.StorageUnsafe()->GetData(), ibm_relative_bno)) == nullptr) {
        panic("inode not in bitmap");
    }

    // TODO(smklein): optional sanity check of both blocks

    // Write the inode back
    if ((status = InodeSync(txn, ino, inode)) != MX_OK) {
        inode_map_.Clear(ino, ino + 1);
        info_.alloc_inode_count--;
        return status;
    }

// Commit blocks to disk
#ifdef __Fuchsia__
    vmoid_t id = inode_map_vmoid_;
#else
    const void* id = inode_map_.StorageUnsafe()->GetData();
#endif
    txn->Enqueue(id, ibm_relative_bno, info_.ibm_block + ibm_relative_bno, 1);

    CountUpdate(txn);
    *ino_out = ino;
    return MX_OK;
}

mx_status_t Minfs::VnodeNew(WriteTxn* txn, mxtl::RefPtr<VnodeMinfs>* out, uint32_t type) {
    if ((type != kMinfsTypeFile) && (type != kMinfsTypeDir)) {
        return MX_ERR_INVALID_ARGS;
    }

    mxtl::RefPtr<VnodeMinfs> vn;
    mx_status_t status;

    // Allocate the in-memory vnode
    if ((status = VnodeMinfs::Allocate(this, type, &vn)) != MX_OK) {
        return status;
    }

    // Allocate the on-disk inode
    if ((status = InoNew(txn, &vn->inode_, &vn->ino_)) != MX_OK) {
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
        return MX_ERR_OUT_OF_RANGE;
    }
    mxtl::RefPtr<VnodeMinfs> vn = mxtl::RefPtr<VnodeMinfs>(vnode_hash_.find(ino).CopyPointer());
    if (vn != nullptr) {
        *out = mxtl::move(vn);
        return MX_OK;
    }
    mx_status_t status;
    if ((status = VnodeMinfs::AllocateHollow(this, &vn)) != MX_OK) {
        return MX_ERR_NO_MEMORY;
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
    vn->ino_ = ino;
    vnode_hash_.insert(vn.get());

    *out = mxtl::move(vn);
    return MX_OK;
}

mx_status_t Minfs::BlockFree(WriteTxn* txn, uint32_t bno) {
    ValidateBno(bno);

#ifdef __Fuchsia__
    auto bbm_id = block_map_vmoid_;
#else
    auto bbm_id = block_map_.StorageUnsafe()->GetData();
#endif

    block_map_.Clear(bno, bno + 1);
    info_.alloc_block_count--;
    uint32_t bitblock = bno / kMinfsBlockBits;
    txn->Enqueue(bbm_id, bitblock, info_.abm_block + bitblock, 1);
    return CountUpdate(txn);
}

// Allocate a new data block from the block bitmap.
//
// If hint is nonzero it indicates which block number to start the search for
// free blocks from.
mx_status_t Minfs::BlockNew(WriteTxn* txn, uint32_t hint, uint32_t* out_bno) {
    size_t bitoff_start;
    mx_status_t status;
    if ((status = block_map_.Find(false, hint, block_map_.size(), 1, &bitoff_start)) != MX_OK) {
        if ((status = block_map_.Find(false, 0, hint, 1, &bitoff_start)) != MX_OK) {
            return MX_ERR_NO_SPACE;
        }
    }

    status = block_map_.Set(bitoff_start, bitoff_start + 1);
    assert(status == MX_OK);
    info_.alloc_block_count++;
    uint32_t bno = static_cast<uint32_t>(bitoff_start);
    ValidateBno(bno);

    // obtain the in-memory bitmap block
    uint32_t bmbno_rel = bno / kMinfsBlockBits;       // bmbno relative to bitmap
    uint32_t bmbno_abs = info_.abm_block + bmbno_rel; // bmbno relative to block device

// commit the bitmap
#ifdef __Fuchsia__
    txn->Enqueue(block_map_vmoid_, bmbno_rel, bmbno_abs, 1);
#else
    void* bmdata = fs::GetBlock<kMinfsBlockSize>(block_map_.StorageUnsafe()->GetData(), bmbno_rel);
    bc_->Writeblk(bmbno_abs, bmdata);
#endif
    ValidateBno(bno);
    *out_bno = bno;

    CountUpdate(txn);
    return MX_OK;
}

mx_status_t Minfs::CountUpdate(WriteTxn* txn) {
    mx_status_t status = MX_OK;

#ifdef __Fuchsia__
    void* infodata = info_vmo_->GetData();
    memcpy(infodata, &info_, sizeof(info_));
    //TODO(planders): look into delaying this transaction.
    txn->Enqueue(info_vmoid_, 0, 0, 1);
#else
    uint8_t blk[kMinfsBlockSize];
    memset(blk, 0, sizeof(blk));
    memcpy(blk, &info_, sizeof(info_));
    status = bc_->Writeblk(0, blk);
#endif

    return status;
}

void minfs_dir_init(void* bdata, uint32_t ino_self, uint32_t ino_parent) {
#define DE0_SIZE DirentSize(1)

    // directory entry for self
    minfs_dirent_t* de = (minfs_dirent_t*)bdata;
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

mx_status_t Minfs::Create(Minfs** out, mxtl::unique_ptr<Bcache> bc, const minfs_info_t* info) {
    uint32_t blocks = bc->Maxblk();
    uint32_t inodes = info->inode_count;

    mx_status_t status = minfs_check_info(info, blocks);
    if (status < 0) {
        return status;
    }

    AllocChecker ac;
    mxtl::unique_ptr<Minfs> fs(new (&ac) Minfs(mxtl::move(bc), info));
    if (!ac.check()) {
        return MX_ERR_NO_MEMORY;
    }
#ifdef __Fuchsia__
    if ((status = fs::VfsDispatcher::Create(mxrio_handler, kPoolSize,
                                            &fs->dispatcher_)) != MX_OK) {
        return status;
    }
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

#ifdef __Fuchsia__
    if ((status = fs->bc_->AttachVmo(fs->block_map_.StorageUnsafe()->GetVmo(),
                                     &fs->block_map_vmoid_)) != MX_OK) {
        return status;
    }
    if ((status = fs->bc_->AttachVmo(fs->inode_map_.StorageUnsafe()->GetVmo(),
                                     &fs->inode_map_vmoid_)) != MX_OK) {
        return status;
    }

    // Create the inode table.
    uint32_t inoblks = (inodes + kMinfsInodesPerBlock - 1) / kMinfsInodesPerBlock;
    if ((status = MappedVmo::Create(inoblks * kMinfsBlockSize, "minfs-inode-table",
                                    &fs->inode_table_)) != MX_OK) {
        return status;
    }

    if ((status = fs->bc_->AttachVmo(fs->inode_table_->GetVmo(),
                                     &fs->inode_table_vmoid_)) != MX_OK) {
        return status;
    }

    // Create the info vmo
    if ((status = MappedVmo::Create(kMinfsBlockSize, "minfs-superblock",
                                    &fs->info_vmo_)) != MX_OK) {
        return status;
    }

    if ((status = fs->bc_->AttachVmo(fs->info_vmo_->GetVmo(),
                                     &fs->info_vmoid_)) != MX_OK) {
        return status;
    }

    ReadTxn txn(fs->bc_.get());
    txn.Enqueue(fs->block_map_vmoid_, 0, fs->info_.abm_block, fs->abmblks_);
    txn.Enqueue(fs->inode_map_vmoid_, 0, fs->info_.ibm_block, fs->ibmblks_);
    txn.Enqueue(fs->inode_table_vmoid_, 0, fs->info_.ino_block, inoblks);
    txn.Enqueue(fs->info_vmoid_, 0, 0, 1);
    if ((status = txn.Flush()) != MX_OK) {
        return status;
    }

#else
    for (uint32_t n = 0; n < fs->abmblks_; n++) {
        void* bmdata = fs::GetBlock<kMinfsBlockSize>(fs->block_map_.StorageUnsafe()->GetData(), n);
        if (fs->bc_->Readblk(fs->info_.abm_block + n, bmdata)) {
            FS_TRACE_ERROR("minfs: failed reading alloc bitmap\n");
        }
    }
    for (uint32_t n = 0; n < fs->ibmblks_; n++) {
        void* bmdata = fs::GetBlock<kMinfsBlockSize>(fs->inode_map_.StorageUnsafe()->GetData(), n);
        if (fs->bc_->Readblk(fs->info_.ibm_block + n, bmdata)) {
            FS_TRACE_ERROR("minfs: failed reading inode bitmap\n");
        }
    }
#endif

    *out = fs.release();
    return MX_OK;
}

mx_status_t minfs_mount(mxtl::RefPtr<VnodeMinfs>* out, mxtl::unique_ptr<Bcache> bc) {
    mx_status_t status;

    char blk[kMinfsBlockSize];
    if ((status = bc->Readblk(0, &blk)) != MX_OK) {
        FS_TRACE_ERROR("minfs: could not read info block\n");
        return status;
    }
    const minfs_info_t* info = reinterpret_cast<minfs_info_t*>(blk);
    minfs_dump_info(info);

    Minfs* fs;
    if ((status = Minfs::Create(&fs, mxtl::move(bc), info)) != MX_OK) {
        FS_TRACE_ERROR("minfs: mount failed\n");
        return status;
    }

    mxtl::RefPtr<VnodeMinfs> vn;
    if ((status = fs->VnodeGet(&vn, kMinfsRootIno)) != MX_OK) {
        FS_TRACE_ERROR("minfs: cannot find root inode\n");
        delete fs;
        return status;
    }

    MX_DEBUG_ASSERT(vn->IsDirectory());
    *out = mxtl::move(vn);
    return MX_OK;
}

mx_status_t Minfs::Unmount() {
#ifdef __Fuchsia__
    dispatcher_ = nullptr;
#endif
    // Explicitly delete this (rather than just letting the memory release when
    // the process exits) to ensure that the block device's fifo has been
    // closed.
    delete this;
    return MX_OK;
}

#ifdef __Fuchsia__
fs::Dispatcher* VnodeMinfs::GetDispatcher() {
    return fs_->GetDispatcher();
}
#endif

int minfs_mkfs(mxtl::unique_ptr<Bcache> bc) {
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
    info.alloc_block_count = 0;
    info.alloc_inode_count = 0;
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
        FS_TRACE_ERROR("mkfs: Failed to allocate block bitmap\n");
        return status;
    }
    if ((status = ibm.Reset(mxtl::roundup(info.inode_count, kMinfsBlockBits))) < 0) {
        FS_TRACE_ERROR("mkfs: Failed to allocate inode bitmap\n");
        return status;
    }
    if ((status = abm.Shrink(info.block_count)) < 0) {
        FS_TRACE_ERROR("mkfs: Failed to shrink block bitmap\n");
        return status;
    }
    if ((status = ibm.Shrink(info.inode_count)) < 0) {
        FS_TRACE_ERROR("mkfs: Failed to shrink inode bitmap\n");
        return status;
    }

    // write rootdir
    uint8_t blk[kMinfsBlockSize];
    memset(blk, 0, sizeof(blk));
    minfs_dir_init(blk, kMinfsRootIno, kMinfsRootIno);
    bc->Writeblk(info.dat_block, blk);

    // update inode bitmap
    ibm.Set(0, 1);
    ibm.Set(kMinfsRootIno, kMinfsRootIno + 1);
    info.alloc_inode_count++;

    // update block bitmap:
    // reserve all blocks before the data storage area
    // reserve the first data block (for root directory)
    abm.Set(0, info.dat_block + 1);
    info.alloc_block_count++;

    // write allocation bitmap
    for (uint32_t n = 0; n < abmblks; n++) {
        void* bmdata = fs::GetBlock<kMinfsBlockSize>(abm.StorageUnsafe()->GetData(), n);
        memcpy(blk, bmdata, kMinfsBlockSize);
        bc->Writeblk(info.abm_block + n, blk);
    }

    // write inode bitmap
    for (uint32_t n = 0; n < ibmblks; n++) {
        void* bmdata = fs::GetBlock<kMinfsBlockSize>(ibm.StorageUnsafe()->GetData(), n);
        memcpy(blk, bmdata, kMinfsBlockSize);
        bc->Writeblk(info.ibm_block + n, blk);
    }

    // write inodes
    memset(blk, 0, sizeof(blk));
    for (uint32_t n = 0; n < inoblks; n++) {
        bc->Writeblk(info.ino_block + n, blk);
    }

    // setup root inode
    minfs_inode_t* ino = reinterpret_cast<minfs_inode_t*>(&blk[0]);
    ino[kMinfsRootIno].magic = kMinfsMagicDir;
    ino[kMinfsRootIno].size = kMinfsBlockSize;
    ino[kMinfsRootIno].block_count = 1;
    ino[kMinfsRootIno].link_count = 2;
    ino[kMinfsRootIno].dirent_count = 2;
    ino[kMinfsRootIno].dnum[0] = info.dat_block;
    bc->Writeblk(info.ino_block, blk);

    memset(blk, 0, sizeof(blk));
    memcpy(blk, &info, sizeof(info));
    bc->Writeblk(0, blk);
    return 0;
}

} // namespace minfs
