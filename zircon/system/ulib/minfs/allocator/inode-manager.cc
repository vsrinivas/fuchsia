// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <utility>

#include <minfs/block-txn.h>

#include "inode-manager.h"

namespace minfs {

#ifdef __Fuchsia__
InodeManager::InodeManager(blk_t start_block)
        : start_block_(start_block) {}
#else
InodeManager::InodeManager(Bcache* bc, blk_t start_block) : start_block_(start_block), bc_(bc) {}
#endif


#ifdef __Fuchsia__
zx_status_t InodeManager::Create(block_client::BlockDevice* device, SuperblockManager* sb,
                                 fs::ReadTxn* txn, AllocatorMetadata metadata, blk_t start_block,
                                 size_t inodes, std::unique_ptr<InodeManager>* out) {
    auto mgr = std::unique_ptr<InodeManager>(new InodeManager(start_block));
#else
zx_status_t InodeManager::Create(Bcache* bc, SuperblockManager* sb, fs::ReadTxn* txn,
                                 AllocatorMetadata metadata, blk_t start_block, size_t inodes,
                                 std::unique_ptr<InodeManager>* out) {
    auto mgr = std::unique_ptr<InodeManager>(new InodeManager(bc, start_block));
#endif
    InodeManager* mgr_raw = mgr.get();

    auto grow_cb = [mgr_raw](uint32_t pool_size) {
        return mgr_raw->Grow(pool_size);
    };

    zx_status_t status;
#ifdef __Fuchsia__
    fbl::unique_ptr<PersistentStorage> storage(new PersistentStorage(device, sb, kMinfsInodeSize,
                                                                     std::move(grow_cb),
                                                                     std::move(metadata)));
#else
    fbl::unique_ptr<PersistentStorage> storage(new PersistentStorage(sb, kMinfsInodeSize,
                                                                     std::move(grow_cb),
                                                                     std::move(metadata)));
#endif
    if ((status = Allocator::Create(txn, std::move(storage), &mgr->inode_allocator_)) != ZX_OK) {
        return status;
    }

#ifdef __Fuchsia__
    uint32_t inoblks = (static_cast<uint32_t>(inodes) + kMinfsInodesPerBlock - 1) /
            kMinfsInodesPerBlock;
    if ((status = mgr->inode_table_.CreateAndMap(inoblks * kMinfsBlockSize, "minfs-inode-table"))
        != ZX_OK) {
        return status;
    }

    zx::vmo xfer_vmo;
    status = mgr->inode_table_.vmo().duplicate(ZX_RIGHT_SAME_RIGHTS, &xfer_vmo);
    if (status != ZX_OK) {
        return status;
    }
    fuchsia_hardware_block_VmoID vmoid;
    status = device->BlockAttachVmo(std::move(xfer_vmo), &vmoid);
    if (status != ZX_OK) {
        return status;
    }
    txn->Enqueue(vmoid.id, 0, start_block, inoblks);
#endif
    *out = std::move(mgr);
    return ZX_OK;
}

void InodeManager::Update(WriteTxn* txn, ino_t ino, const Inode* inode) {
    // Obtain the offset of the inode within its containing block
    const uint32_t off_of_ino = (ino % kMinfsInodesPerBlock) * kMinfsInodeSize;
    const blk_t inoblock_rel = ino / kMinfsInodesPerBlock;
    const blk_t inoblock_abs = inoblock_rel + start_block_;
    assert(inoblock_abs < kFVMBlockDataStart);
#ifdef __Fuchsia__
    void* inodata = (void*)((uintptr_t)(inode_table_.start()) +
                            (uintptr_t)(inoblock_rel * kMinfsBlockSize));
    memcpy((void*)((uintptr_t)inodata + off_of_ino), inode, kMinfsInodeSize);
    txn->Enqueue(inode_table_.vmo().get(), inoblock_rel, inoblock_abs, 1);
#else
    // Since host-side tools don't have "mapped vmos", just read / update /
    // write the single absolute inode block.
    uint8_t inodata[kMinfsBlockSize];
    bc_->Readblk(inoblock_abs, inodata);
    memcpy((void*)((uintptr_t)inodata + off_of_ino), inode, kMinfsInodeSize);
    bc_->Writeblk(inoblock_abs, inodata);
#endif
}

const Allocator* InodeManager::GetInodeAllocator() const {
    return inode_allocator_.get();
}

void InodeManager::Load(ino_t ino, Inode* out) const {
    // obtain the block of the inode table we need
    uint32_t off_of_ino = (ino % kMinfsInodesPerBlock) * kMinfsInodeSize;
#ifdef __Fuchsia__
    void* inodata = (void*)((uintptr_t)(inode_table_.start()) +
                            (uintptr_t)((ino / kMinfsInodesPerBlock) * kMinfsBlockSize));
#else
    uint8_t inodata[kMinfsBlockSize];
    bc_->Readblk(start_block_ + (ino / kMinfsInodesPerBlock), inodata);
#endif
    const Inode* inode = reinterpret_cast<const Inode*>((uintptr_t)inodata +
                                                                        off_of_ino);
    memcpy(out, inode, kMinfsInodeSize);
}

zx_status_t InodeManager::Grow(size_t inodes) {
#ifdef __Fuchsia__
    uint32_t inoblks = (static_cast<uint32_t>(inodes) + kMinfsInodesPerBlock - 1) /
            kMinfsInodesPerBlock;
    if (inode_table_.Grow(inoblks * kMinfsBlockSize) != ZX_OK) {
        return ZX_ERR_NO_SPACE;
    }
    return ZX_OK;
#else
    return ZX_ERR_NO_SPACE;
#endif
}

} // namespace minfs
