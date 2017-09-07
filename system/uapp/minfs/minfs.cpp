// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <bitmap/raw-bitmap.h>
#include <fs/block-txn.h>
#include <fs/trace.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/limits.h>
#include <fbl/unique_ptr.h>

#include "minfs-private.h"

namespace minfs {

fs::Vfs vfs;

void minfs_dump_info(const minfs_info_t* info) {
    FS_TRACE(MINFS, "minfs: data blocks:  %10u (size %u)\n", info->block_count, info->block_size);
    FS_TRACE(MINFS, "minfs: inodes:  %10u (size %u)\n", info->inode_count, info->inode_size);
    FS_TRACE(MINFS, "minfs: allocated blocks  @ %10u\n", info->alloc_block_count);
    FS_TRACE(MINFS, "minfs: allocated inodes  @ %10u\n", info->alloc_inode_count);
    FS_TRACE(MINFS, "minfs: inode bitmap @ %10u\n", info->ibm_block);
    FS_TRACE(MINFS, "minfs: alloc bitmap @ %10u\n", info->abm_block);
    FS_TRACE(MINFS, "minfs: inode table  @ %10u\n", info->ino_block);
    FS_TRACE(MINFS, "minfs: data blocks  @ %10u\n", info->dat_block);
    FS_TRACE(MINFS, "minfs: FVM-aware: %s\n", (info->flags & kMinfsFlagFVM) ? "YES" : "NO");
}

void minfs_dump_inode(const minfs_inode_t* inode, ino_t ino) {
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
    if ((info->flags & kMinfsFlagFVM) == 0) {
        if (info->dat_block + info->block_count > max) {
            FS_TRACE_ERROR("minfs: too large for device\n");
            return MX_ERR_INVALID_ARGS;
        }
    } else {
        // TODO(smklein): Verify slice size, vslice count.

        // Verify that the allocated slices are sufficient to hold
        // the allocated data structures of the filesystem.
        size_t ibm_blocks_needed = (info->inode_count + kMinfsBlockBits - 1) / kMinfsBlockBits;
        const size_t kBlocksPerSlice = info->slice_size / kMinfsBlockSize;
        size_t ibm_blocks_allocated = info->ibm_slices * kBlocksPerSlice;
        if (ibm_blocks_needed > ibm_blocks_allocated) {
            FS_TRACE_ERROR("minfs: Not enough slices for inode bitmap\n");
            return MX_ERR_INVALID_ARGS;
        } else if (ibm_blocks_allocated + info->ibm_block >= info->abm_block) {
            FS_TRACE_ERROR("minfs: Inode bitmap collides into block bitmap\n");
            return MX_ERR_INVALID_ARGS;
        }
        size_t abm_blocks_needed = (info->block_count + kMinfsBlockBits - 1) / kMinfsBlockBits;
        size_t abm_blocks_allocated = info->abm_slices * kBlocksPerSlice;
        if (abm_blocks_needed > abm_blocks_allocated) {
            FS_TRACE_ERROR("minfs: Not enough slices for block bitmap\n");
            return MX_ERR_INVALID_ARGS;
        } else if (abm_blocks_allocated + info->abm_block >= info->ino_block) {
            FS_TRACE_ERROR("minfs: Block bitmap collides with inode table\n");
            return MX_ERR_INVALID_ARGS;
        }
        size_t ino_blocks_needed = (info->inode_count + kMinfsInodesPerBlock - 1) / kMinfsInodesPerBlock;
        size_t ino_blocks_allocated = info->ino_slices * kBlocksPerSlice;
        if (ino_blocks_needed > ino_blocks_allocated) {
            FS_TRACE_ERROR("minfs: Not enough slices for inode table\n");
            return MX_ERR_INVALID_ARGS;
        } else if (ino_blocks_allocated + info->ino_block >= info->dat_block) {
            FS_TRACE_ERROR("minfs: Inode table collides with data blocks\n");
            return MX_ERR_INVALID_ARGS;
        }
        size_t dat_blocks_needed = info->block_count;
        size_t dat_blocks_allocated = info->dat_slices * kBlocksPerSlice;
        if (dat_blocks_needed > dat_blocks_allocated) {
            FS_TRACE_ERROR("minfs: Not enough slices for data blocks\n");
            return MX_ERR_INVALID_ARGS;
        } else if (dat_blocks_allocated + info->dat_block >
                   fbl::numeric_limits<blk_t>::max()) {
            FS_TRACE_ERROR("minfs: Data blocks overflow blk_t\n");
            return MX_ERR_INVALID_ARGS;
        }
    }
    //TODO: validate layout
    return 0;
}

mx_status_t Minfs::InodeSync(WriteTxn* txn, ino_t ino, const minfs_inode_t* inode) {
    // Obtain the offset of the inode within its containing block
    const uint32_t off_of_ino = (ino % kMinfsInodesPerBlock) * kMinfsInodeSize;
    const blk_t inoblock_rel = ino / kMinfsInodesPerBlock;
    const blk_t inoblock_abs = inoblock_rel + info_.ino_block;
    assert(inoblock_abs < kFVMBlockDataStart);
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

Minfs::Minfs(fbl::unique_ptr<Bcache> bc, const minfs_info_t* info) : bc_(fbl::move(bc)) {
    memcpy(&info_, info, sizeof(minfs_info_t));
}

Minfs::~Minfs() {
    vnode_hash_.clear();
}

mx_status_t Minfs::InoFree(VnodeMinfs* vn) {
    // We're going to be updating block bitmaps repeatedly.
    WriteTxn txn(bc_.get());
#ifdef __Fuchsia__
    auto ibm_id = inode_map_vmoid_;
#else
    auto ibm_id = inode_map_.StorageUnsafe()->GetData();
#endif

    // Free the inode bit itself
    inode_map_.Clear(vn->ino_, vn->ino_ + 1);
    info_.alloc_inode_count--;

    blk_t bitbno = vn->ino_ / kMinfsBlockBits;
    txn.Enqueue(ibm_id, bitbno, info_.ibm_block + bitbno, 1);
    uint32_t block_count = vn->inode_.block_count;

    // release all direct blocks
    for (unsigned n = 0; n < kMinfsDirect; n++) {
        if (vn->inode_.dnum[n] == 0) {
            continue;
        }
        ValidateBno(vn->inode_.dnum[n]);
        block_count--;
        BlockFree(&txn, vn->inode_.dnum[n]);
    }


    // release all indirect blocks
    for (unsigned n = 0; n < kMinfsIndirect; n++) {
        if (vn->inode_.inum[n] == 0) {
            continue;
        }

#ifdef __Fuchsia__
        mx_status_t status;
        if ((status = vn->InitIndirectVmo()) != MX_OK) {
            return status;
        }

        uint32_t* entry;
        vn->ReadIndirectVmoBlock(n, &entry);
#else
        uint32_t entry[kMinfsBlockSize];
        vn->ReadIndirectBlock(vn->inode_.inum[n], entry);
#endif

        // release the direct blocks pointed at by the entries in the indirect block
        for (unsigned m = 0; m < kMinfsDirectPerIndirect; m++) {
            if (entry[m] == 0) {
                continue;
            }
            block_count--;
            BlockFree(&txn, entry[m]);
        }
        // release the direct block itself
        block_count--;
        BlockFree(&txn, vn->inode_.inum[n]);

    }

    // release doubly indirect blocks
    for (unsigned n = 0; n < kMinfsDoublyIndirect; n++) {
        if (vn->inode_.dinum[n] == 0) {
            continue;
        }
#ifdef __Fuchsia__
        mx_status_t status;
        if ((status = vn->InitIndirectVmo()) != MX_OK) {
            return status;
        }

        uint32_t* dentry;
        vn->ReadIndirectVmoBlock(GetVmoOffsetForDoublyIndirect(n), &dentry);
#else
        uint32_t dentry[kMinfsBlockSize];
        vn->ReadIndirectBlock(vn->inode_.dinum[n], dentry);
#endif
        // release indirect blocks
        for (unsigned m = 0; m < kMinfsDirectPerIndirect; m++) {
            if (dentry[m] == 0) {
                continue;
            }

#ifdef __Fuchsia__
            if ((status = vn->LoadIndirectWithinDoublyIndirect(n)) != MX_OK) {
                return status;
            }

            uint32_t* entry;
            vn->ReadIndirectVmoBlock(GetVmoOffsetForIndirect(n) + m, &entry);

#else
            uint32_t entry[kMinfsBlockSize];
            vn->ReadIndirectBlock(dentry[m], entry);
#endif

            // release direct blocks
            for (unsigned k = 0; k < kMinfsDirectPerIndirect; k++) {
                if (entry[k] == 0) {
                    continue;
                }

                block_count--;
                BlockFree(&txn, entry[k]);
            }

            block_count--;
            BlockFree(&txn, dentry[m]);
        }

        // release the doubly indirect block itself
        block_count--;
        BlockFree(&txn, vn->inode_.dinum[n]);
    }

    CountUpdate(&txn);
    MX_DEBUG_ASSERT(block_count == 0);
    return MX_OK;
}

mx_status_t Minfs::AddInodes() {
#ifdef __Fuchsia__
    if ((info_.flags & kMinfsFlagFVM) == 0) {
        return MX_ERR_NO_SPACE;
    }

    const size_t kBlocksPerSlice = info_.slice_size / kMinfsBlockSize;
    extend_request_t request;
    request.length = 1;
    request.offset = (kFVMBlockInodeStart / kBlocksPerSlice) + info_.ino_slices;

    const uint32_t kInodesPerSlice = static_cast<uint32_t>(info_.slice_size /
                                                           kMinfsInodeSize);
    uint32_t inodes = (info_.ino_slices + static_cast<uint32_t>(request.length))
            * kInodesPerSlice;
    uint32_t ibmblks = (inodes + kMinfsBlockBits - 1) / kMinfsBlockBits;
    uint32_t ibmblks_old = (info_.inode_count + kMinfsBlockBits - 1) / kMinfsBlockBits;
    MX_DEBUG_ASSERT(ibmblks_old <= ibmblks);
    if (ibmblks > kBlocksPerSlice) {
        // TODO(smklein): Increase the size of the inode bitmap,
        // in addition to the size of the inode table.
        fprintf(stderr, "Minfs::AddInodes needs to increase inode bitmap size");
        return MX_ERR_NO_SPACE;
    }

    if (bc_->FVMExtend(&request) != MX_OK) {
        // TODO(smklein): Query FVM on reboot to verify our
        // superblock matches our allocated extents.
        fprintf(stderr, "Minfs::AddInodes FVM Extend failure");
        return MX_ERR_NO_SPACE;
    }

    WriteTxn txn(bc_.get());

    // Update the inode bitmap, write the new blocks back to disk
    // as "zero".
    if (inode_map_.Grow(fbl::roundup(inodes, kMinfsBlockBits)) != MX_OK) {
        return MX_ERR_NO_SPACE;
    }
    // Grow before shrinking to ensure the underlying storage is a multiple
    // of kMinfsBlockSize.
    inode_map_.Shrink(inodes);
    if (ibmblks > ibmblks_old) {
        txn.Enqueue(inode_map_vmoid_, ibmblks_old, info_.ibm_block + ibmblks_old,
                    ibmblks - ibmblks_old);
    }

    // Update the inode table
    uint32_t inoblks = (inodes + kMinfsInodesPerBlock - 1) / kMinfsInodesPerBlock;
    if (inode_table_->Grow(inoblks * kMinfsBlockSize) != MX_OK) {
        return MX_ERR_NO_SPACE;
    }

    info_.vslice_count += request.length;
    info_.ino_slices += static_cast<uint32_t>(request.length);
    info_.inode_count = inodes;
    ibmblks_ = ibmblks;
    txn.Enqueue(info_vmoid_, 0, 0, 1);

    return txn.Flush();
#else
    return MX_ERR_NO_SPACE;
#endif
}

mx_status_t Minfs::AddBlocks() {
#ifdef __Fuchsia__
    if ((info_.flags & kMinfsFlagFVM) == 0) {
        return MX_ERR_NO_SPACE;
    }

    const size_t kBlocksPerSlice = info_.slice_size / kMinfsBlockSize;
    extend_request_t request;
    request.length = 1;
    request.offset = (kFVMBlockDataStart / kBlocksPerSlice) + info_.dat_slices;
    uint64_t blocks64 = (info_.dat_slices + request.length) * kBlocksPerSlice;
    MX_DEBUG_ASSERT(blocks64 <= fbl::numeric_limits<uint32_t>::max());
    uint32_t blocks = static_cast<uint32_t>(blocks64);
    uint32_t abmblks = (blocks + kMinfsBlockBits - 1) / kMinfsBlockBits;
    uint32_t abmblks_old = (info_.block_count + kMinfsBlockBits - 1) / kMinfsBlockBits;
    MX_DEBUG_ASSERT(abmblks_old <= abmblks);

    if (abmblks > kBlocksPerSlice) {
        // TODO(smklein): Increase the size of the block bitmap.
        fprintf(stderr, "Minfs::AddBlocks needs to increase block bitmap size");
        return MX_ERR_NO_SPACE;
    }

    if (bc_->FVMExtend(&request) != MX_OK) {
        // TODO(smklein): Query FVM on reboot to verify our
        // superblock matches our allocated extents.
        fprintf(stderr, "Minfs::AddBlocks FVM Extend failure");
        return MX_ERR_NO_SPACE;
    }

    WriteTxn txn(bc_.get());

    // Update the block bitmap, write the new blocks back to disk
    // as "zero".
    if (block_map_.Grow(fbl::roundup(blocks, kMinfsBlockBits)) != MX_OK) {
        return MX_ERR_NO_SPACE;
    }
    // Grow before shrinking to ensure the underlying storage is a multiple
    // of kMinfsBlockSize.
    block_map_.Shrink(blocks);
    if (abmblks > abmblks_old) {
        txn.Enqueue(block_map_vmoid_, abmblks_old, info_.abm_block + abmblks_old,
                    abmblks - abmblks_old);
    }

    info_.vslice_count += request.length;
    info_.dat_slices += static_cast<uint32_t>(request.length);
    info_.block_count = blocks;

    abmblks_ = abmblks;
    txn.Enqueue(info_vmoid_, 0, 0, 1);
    return txn.Flush();
#else
    return MX_ERR_NO_SPACE;
#endif
}

mx_status_t Minfs::InoNew(WriteTxn* txn, const minfs_inode_t* inode, ino_t* ino_out) {
    size_t bitoff_start;
    mx_status_t status = inode_map_.Find(false, 0, inode_map_.size(), 1, &bitoff_start);
    if (status != MX_OK) {
        size_t old_size = inode_map_.size();
        if ((status = AddInodes()) != MX_OK) {
            return status;
        } else if ((status = inode_map_.Find(false, old_size, inode_map_.size(),
                                             1, &bitoff_start)) != MX_OK) {
            return status;
        }
    }

    status = inode_map_.Set(bitoff_start, bitoff_start + 1);
    assert(status == MX_OK);
    info_.alloc_inode_count++;
    ino_t ino = static_cast<ino_t>(bitoff_start);

    // locate data and block offset of bitmap
    void* bmdata;
    MX_DEBUG_ASSERT(ino <= inode_map_.size());
    blk_t ibm_relative_bno = (ino / kMinfsBlockBits);
    if ((bmdata = fs::GetBlock<kMinfsBlockSize>(inode_map_.StorageUnsafe()->GetData(),
        ibm_relative_bno)) == nullptr) {
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

mx_status_t Minfs::VnodeNew(WriteTxn* txn, fbl::RefPtr<VnodeMinfs>* out, uint32_t type) {
    if ((type != kMinfsTypeFile) && (type != kMinfsTypeDir)) {
        return MX_ERR_INVALID_ARGS;
    }

    fbl::RefPtr<VnodeMinfs> vn;
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

    *out = fbl::move(vn);
    return 0;
}

void Minfs::VnodeRelease(VnodeMinfs* vn) {
    vnode_hash_.erase(*vn);
}

mx_status_t Minfs::VnodeGet(fbl::RefPtr<VnodeMinfs>* out, ino_t ino) {
    if ((ino < 1) || (ino >= info_.inode_count)) {
        return MX_ERR_OUT_OF_RANGE;
    }
    fbl::RefPtr<VnodeMinfs> vn = fbl::RefPtr<VnodeMinfs>(vnode_hash_.find(ino).CopyPointer());
    if (vn != nullptr) {
        *out = fbl::move(vn);
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

    *out = fbl::move(vn);
    return MX_OK;
}

mx_status_t Minfs::BlockFree(WriteTxn* txn, blk_t bno) {
    ValidateBno(bno);

#ifdef __Fuchsia__
    auto bbm_id = block_map_vmoid_;
#else
    auto bbm_id = block_map_.StorageUnsafe()->GetData();
#endif

    block_map_.Clear(bno, bno + 1);
    info_.alloc_block_count--;
    blk_t bitbno = bno / kMinfsBlockBits;
    txn->Enqueue(bbm_id, bitbno, info_.abm_block + bitbno, 1);
    return CountUpdate(txn);
}

// Allocate a new data block from the block bitmap.
//
// If hint is nonzero it indicates which block number to start the search for
// free blocks from.
mx_status_t Minfs::BlockNew(WriteTxn* txn, blk_t hint, blk_t* out_bno) {
    size_t bitoff_start;
    mx_status_t status;
    if ((status = block_map_.Find(false, hint, block_map_.size(), 1, &bitoff_start)) != MX_OK) {
        if ((status = block_map_.Find(false, 0, hint, 1, &bitoff_start)) != MX_OK) {
            size_t old_size = block_map_.size();
            if ((status = AddBlocks()) != MX_OK) {
                return status;
            } else if ((status = block_map_.Find(false, old_size, block_map_.size(),
                                                 1, &bitoff_start)) != MX_OK) {
                return status;
            }
        }
    }

    status = block_map_.Set(bitoff_start, bitoff_start + 1);
    assert(status == MX_OK);
    info_.alloc_block_count++;
    blk_t bno = static_cast<blk_t>(bitoff_start);
    ValidateBno(bno);

    // obtain the in-memory bitmap block
    blk_t bmbno_rel = bno / kMinfsBlockBits;       // bmbno relative to bitmap
    blk_t bmbno_abs = info_.abm_block + bmbno_rel; // bmbno relative to block device

// commit the bitmap
#ifdef __Fuchsia__
    txn->Enqueue(block_map_vmoid_, bmbno_rel, bmbno_abs, 1);
#else
    void* bmdata = fs::GetBlock<kMinfsBlockSize>(block_map_.StorageUnsafe()->GetData(), bmbno_rel);
    bc_->Writeblk(bmbno_abs, bmdata);
#endif
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

void minfs_dir_init(void* bdata, ino_t ino_self, ino_t ino_parent) {
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

mx_status_t Minfs::Create(Minfs** out, fbl::unique_ptr<Bcache> bc, const minfs_info_t* info) {
    mx_status_t status = minfs_check_info(info, bc->Maxblk());
    if (status < 0) {
        return status;
    }

    fbl::AllocChecker ac;
    fbl::unique_ptr<Minfs> fs(new (&ac) Minfs(fbl::move(bc), info));
    if (!ac.check()) {
        return MX_ERR_NO_MEMORY;
    }
    // determine how many blocks of inodes, allocation bitmaps,
    // and inode bitmaps there are
    uint32_t blocks = info->block_count;
    uint32_t inodes = info->inode_count;
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

mx_status_t minfs_mount(fbl::RefPtr<VnodeMinfs>* out, fbl::unique_ptr<Bcache> bc) {
    mx_status_t status;

    char blk[kMinfsBlockSize];
    if ((status = bc->Readblk(0, &blk)) != MX_OK) {
        FS_TRACE_ERROR("minfs: could not read info block\n");
        return status;
    }
    const minfs_info_t* info = reinterpret_cast<minfs_info_t*>(blk);

    Minfs* fs;
    if ((status = Minfs::Create(&fs, fbl::move(bc), info)) != MX_OK) {
        FS_TRACE_ERROR("minfs: mount failed\n");
        return status;
    }

    fbl::RefPtr<VnodeMinfs> vn;
    if ((status = fs->VnodeGet(&vn, kMinfsRootIno)) != MX_OK) {
        FS_TRACE_ERROR("minfs: cannot find root inode\n");
        delete fs;
        return status;
    }

    MX_DEBUG_ASSERT(vn->IsDirectory());
    *out = fbl::move(vn);
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
    // TODO(smklein): To not bind filesystem lifecycle to a process, shut
    // down (closing dispatcher) rather than calling exit.
    exit(0);
    return MX_OK;
}

void minfs_free_slices(Bcache* bc, const minfs_info_t* info) {
    if ((info->flags & kMinfsFlagFVM) == 0) {
        return;
    }
#ifdef __Fuchsia__
    extend_request_t request;
    const size_t kBlocksPerSlice = info->slice_size / kMinfsBlockSize;
    if (info->ibm_slices) {
        request.length = info->ibm_slices;
        request.offset = kFVMBlockInodeBmStart / kBlocksPerSlice;
        bc->FVMShrink(&request);
    }
    if (info->abm_slices) {
        request.length = info->abm_slices;
        request.offset = kFVMBlockDataBmStart / kBlocksPerSlice;
        bc->FVMShrink(&request);
    }
    if (info->ino_slices) {
        request.length = info->ino_slices;
        request.offset = kFVMBlockInodeStart / kBlocksPerSlice;
        bc->FVMShrink(&request);
    }
    if (info->dat_slices) {
        request.length = info->dat_slices;
        request.offset = kFVMBlockDataStart / kBlocksPerSlice;
        bc->FVMShrink(&request);
    }
#endif
}

int minfs_mkfs(fbl::unique_ptr<Bcache> bc) {
    minfs_info_t info;
    memset(&info, 0x00, sizeof(info));
    info.magic0 = kMinfsMagic0;
    info.magic1 = kMinfsMagic1;
    info.version = kMinfsVersion;
    info.flags = kMinfsFlagClean;
    info.block_size = kMinfsBlockSize;
    info.inode_size = kMinfsInodeSize;

    uint32_t blocks = 0;
    uint32_t inodes = 0;

#ifdef __Fuchsia__
    fvm_info_t fvm_info;
    if (bc->FVMQuery(&fvm_info) == MX_OK) {
        info.slice_size = fvm_info.slice_size;
        info.flags |= kMinfsFlagFVM;


        if (info.slice_size % kMinfsBlockSize) {
            fprintf(stderr, "minfs mkfs: Slice size not multiple of minfs block\n");
            return -1;
        }

        const size_t kBlocksPerSlice = info.slice_size / kMinfsBlockSize;
        extend_request_t request;
        request.length = 1;
        request.offset = kFVMBlockInodeBmStart / kBlocksPerSlice;
        if (bc->FVMExtend(&request) != MX_OK) {
            fprintf(stderr, "minfs mkfs: Failed to allocate inode bitmap\n");
            return -1;
        }
        info.ibm_slices = 1;
        request.offset = kFVMBlockDataBmStart / kBlocksPerSlice;
        if (bc->FVMExtend(&request) != MX_OK) {
            fprintf(stderr, "minfs mkfs: Failed to allocate data bitmap\n");
            minfs_free_slices(bc.get(), &info);
            return -1;
        }
        info.abm_slices = 1;
        request.offset = kFVMBlockInodeStart / kBlocksPerSlice;
        if (bc->FVMExtend(&request) != MX_OK) {
            fprintf(stderr, "minfs mkfs: Failed to allocate inode table\n");
            minfs_free_slices(bc.get(), &info);
            return -1;
        }
        info.ino_slices = 1;
        request.offset = kFVMBlockDataStart / kBlocksPerSlice;
        if (bc->FVMExtend(&request) != MX_OK) {
            fprintf(stderr, "minfs mkfs: Failed to allocate data blocks\n");
            minfs_free_slices(bc.get(), &info);
            return -1;
        }
        info.dat_slices = 1;

        info.vslice_count = 1 + info.ibm_slices + info.abm_slices +
                            info.ino_slices + info.dat_slices;

        inodes = static_cast<uint32_t>(info.ino_slices * info.slice_size / kMinfsInodeSize);
        blocks = static_cast<uint32_t>(info.dat_slices * info.slice_size / kMinfsBlockSize);
    }
#endif
    if ((info.flags & kMinfsFlagFVM) == 0) {
        inodes = 32768;
        blocks = bc->Maxblk();
    }

    // determine how many blocks of inodes, allocation bitmaps,
    // and inode bitmaps there are
    uint32_t inoblks = (inodes + kMinfsInodesPerBlock - 1) / kMinfsInodesPerBlock;
    uint32_t ibmblks = (inodes + kMinfsBlockBits - 1) / kMinfsBlockBits;
    uint32_t abmblks;

    info.inode_count = inodes;
    info.alloc_block_count = 0;
    info.alloc_inode_count = 0;
    if ((info.flags & kMinfsFlagFVM) == 0) {
        // Aligning distinct data areas to 8 block groups.
        uint32_t non_dat_blocks = (8 + fbl::roundup(ibmblks, 8u) + inoblks);
        if (non_dat_blocks >= blocks) {
            fprintf(stderr, "mkfs: Partition size (%" PRIu64 " bytes) is too small\n",
                    static_cast<uint64_t>(blocks) * kMinfsBlockSize);
            return MX_ERR_INVALID_ARGS;
        }

        uint32_t dat_block_count = blocks - non_dat_blocks;
        abmblks = (dat_block_count + kMinfsBlockBits - 1) / kMinfsBlockBits;
        info.block_count = dat_block_count - fbl::roundup(abmblks, 8u);
        info.ibm_block = 8;
        info.abm_block = info.ibm_block + fbl::roundup(ibmblks, 8u);
        info.ino_block = info.abm_block + fbl::roundup(abmblks, 8u);
        info.dat_block = info.ino_block + inoblks;
    } else {
        info.block_count = blocks;
        abmblks = (info.block_count + kMinfsBlockBits - 1) / kMinfsBlockBits;
        info.ibm_block = kFVMBlockInodeBmStart;
        info.abm_block = kFVMBlockDataBmStart;
        info.ino_block = kFVMBlockInodeStart;
        info.dat_block = kFVMBlockDataStart;
    }

    minfs_dump_info(&info);

    RawBitmap abm;
    RawBitmap ibm;

    // By allocating the bitmap and then shrinking it, we keep the underlying
    // storage a block multiple but ensure we can't allocate beyond the last
    // real block or inode.
    mx_status_t status;
    if ((status = abm.Reset(fbl::roundup(info.block_count, kMinfsBlockBits))) < 0) {
        FS_TRACE_ERROR("mkfs: Failed to allocate block bitmap\n");
        minfs_free_slices(bc.get(), &info);
        return status;
    }
    if ((status = ibm.Reset(fbl::roundup(info.inode_count, kMinfsBlockBits))) < 0) {
        FS_TRACE_ERROR("mkfs: Failed to allocate inode bitmap\n");
        minfs_free_slices(bc.get(), &info);
        return status;
    }
    if ((status = abm.Shrink(info.block_count)) < 0) {
        FS_TRACE_ERROR("mkfs: Failed to shrink block bitmap\n");
        minfs_free_slices(bc.get(), &info);
        return status;
    }
    if ((status = ibm.Shrink(info.inode_count)) < 0) {
        FS_TRACE_ERROR("mkfs: Failed to shrink inode bitmap\n");
        minfs_free_slices(bc.get(), &info);
        return status;
    }

    // write rootdir
    uint8_t blk[kMinfsBlockSize];
    memset(blk, 0, sizeof(blk));
    minfs_dir_init(blk, kMinfsRootIno, kMinfsRootIno);
    bc->Writeblk(info.dat_block + 1, blk);

    // update inode bitmap
    ibm.Set(0, 1);
    ibm.Set(kMinfsRootIno, kMinfsRootIno + 1);
    info.alloc_inode_count++;

    // update block bitmap:
    // Reserve the 0th data block (as a 'null' value)
    // Reserve the 1st data block (for root directory)
    abm.Set(0, 2);
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
    ino[kMinfsRootIno].dnum[0] = 1;
    bc->Writeblk(info.ino_block, blk);

    memset(blk, 0, sizeof(blk));
    memcpy(blk, &info, sizeof(info));
    bc->Writeblk(0, blk);
    return 0;
}

} // namespace minfs
