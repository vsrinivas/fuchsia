// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#include <fs/block-txn.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <zircon/device/vfs.h>

#ifdef __Fuchsia__
#include <zircon/syscalls.h>
#include <lib/fdio/vfs.h>
#include <fbl/auto_lock.h>
#endif

#include "minfs-private.h"

// #define DEBUG_PRINTF
#ifdef DEBUG_PRINTF
#define xprintf(args...) fprintf(stderr, args)
#else
#define xprintf(args...)
#endif

namespace {

zx_time_t minfs_gettime_utc() {
    // linux/zircon compatible
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    zx_time_t time = ZX_SEC(ts.tv_sec)+ts.tv_nsec;
    return time;
}

} // namespace anonymous

namespace minfs {

void VnodeMinfs::SetIno(ino_t ino) {
    ZX_DEBUG_ASSERT(ino_ == 0);
    ino_ = ino;
}

void VnodeMinfs::InodeSync(WritebackWork* wb, uint32_t flags) {
    // by default, c/mtimes are not updated to current time
    if (flags != kMxFsSyncDefault) {
        zx_time_t cur_time = minfs_gettime_utc();
        // update times before syncing
        if ((flags & kMxFsSyncMtime) != 0) {
            inode_.modify_time = cur_time;
        }
        if ((flags & kMxFsSyncCtime) != 0) {
            inode_.create_time = cur_time;
        }
    }

    fs_->InodeUpdate(wb, ino_, &inode_);
}

// Delete all blocks (relative to a file) from "start" (inclusive) to the end of
// the file. Does not update mtime/atime.
zx_status_t VnodeMinfs::BlocksShrink(WritebackWork* wb, blk_t start) {
    ZX_DEBUG_ASSERT(wb != nullptr);
    bop_params_t boparams(start, static_cast<blk_t>(kMinfsMaxFileBlock - start), nullptr);
    zx_status_t status;
    if ((status = BlockOp(wb, DELETE, &boparams)) != ZX_OK) {
        return status;
    }

#ifdef __Fuchsia__
    // Arbitrary minimum size for indirect vmo
    size_t size = (kMinfsIndirect + kMinfsDoublyIndirect) * kMinfsBlockSize;
    // Number of blocks before dindirect blocks start
    blk_t pre_dindirect = kMinfsDirect + kMinfsDirectPerIndirect * kMinfsIndirect;
    if (start > pre_dindirect) {
        blk_t distart = start - pre_dindirect; //first bno relative to dindirect blocks
        blk_t last_dindirect = distart / (kMinfsDirectPerDindirect); // index of last dindirect

        // Calculate new size for indirect vmo
        if (distart % kMinfsDirectPerDindirect) {
            size = GetVmoSizeForIndirect(last_dindirect);
        } else if (last_dindirect) {
            size = GetVmoSizeForIndirect(last_dindirect - 1);
        }
    }

    // Shrink the indirect vmo if necessary
    if (vmo_indirect_ != nullptr && vmo_indirect_->GetSize() > size) {
        if ((status = vmo_indirect_->Shrink(size)) != ZX_OK) {
            return status;
        }
    }
#endif
    return ZX_OK;
}

#ifdef __Fuchsia__
zx_status_t VnodeMinfs::LoadIndirectBlocks(blk_t* iarray, uint32_t count, uint32_t offset,
                                           uint64_t size) {
    zx_status_t status;
    if ((status = InitIndirectVmo()) != ZX_OK) {
        return status;
    }

    if (vmo_indirect_->GetSize() < size) {
        zx_status_t status;
        if ((status = vmo_indirect_->Grow(size)) != ZX_OK) {
            return status;
        }
    }

    ReadTxn txn(fs_->bc_.get());

    for (uint32_t i = 0; i < count; i++) {
        blk_t ibno;
        if ((ibno = iarray[i]) != 0) {
            fs_->ValidateBno(ibno);
            txn.Enqueue(vmoid_indirect_, offset + i, ibno + fs_->Info().dat_block, 1);
        }
    }

    return txn.Flush();
}

zx_status_t VnodeMinfs::LoadIndirectWithinDoublyIndirect(uint32_t dindex) {
    uint32_t* dientry;

    size_t size = GetVmoSizeForIndirect(dindex);
    if (vmo_indirect_->GetSize() >= size) {
        // We've already loaded this indirect (within dind) block.
        return ZX_OK;
    }

    ReadIndirectVmoBlock(GetVmoOffsetForDoublyIndirect(dindex), &dientry);
    return LoadIndirectBlocks(dientry, kMinfsDirectPerIndirect,
                              GetVmoOffsetForIndirect(dindex), size);
}

zx_status_t VnodeMinfs::InitIndirectVmo() {
    if (vmo_indirect_ != nullptr) {
        return ZX_OK;
    }

    zx_status_t status;
    if ((status = fs::MappedVmo::Create(kMinfsBlockSize * (kMinfsIndirect + kMinfsDoublyIndirect),
                                        "minfs-indirect", &vmo_indirect_)) != ZX_OK) {
        return status;
    }
    if ((status = fs_->bc_->AttachVmo(vmo_indirect_->GetVmo(), &vmoid_indirect_)) != ZX_OK) {
        vmo_indirect_ = nullptr;
        return status;
    }

    // Load initial set of indirect blocks
    if ((status = LoadIndirectBlocks(inode_.inum, kMinfsIndirect, 0, 0)) != ZX_OK) {
        vmo_indirect_ = nullptr;
        return status;
    }

    // Load doubly indirect blocks
    if ((status = LoadIndirectBlocks(inode_.dinum, kMinfsDoublyIndirect,
                                     GetVmoOffsetForDoublyIndirect(0),
                                     GetVmoSizeForDoublyIndirect()) != ZX_OK)) {
        vmo_indirect_ = nullptr;
        return status;
    }

    return ZX_OK;
}

// Since we cannot yet register the filesystem as a paging service (and cleanly
// fault on pages when they are actually needed), we currently read an entire
// file to a VMO when a file's data block are accessed.
//
// TODO(smklein): Even this hack can be optimized; a bitmap could be used to
// track all 'empty/read/dirty' blocks for each vnode, rather than reading
// the entire file.
zx_status_t VnodeMinfs::InitVmo() {
    if (vmo_.is_valid()) {
        return ZX_OK;
    }

    zx_status_t status;
    const size_t vmo_size = fbl::round_up(inode_.size, kMinfsBlockSize);
    if ((status = zx::vmo::create(vmo_size, 0, &vmo_)) != ZX_OK) {
        FS_TRACE_ERROR("Failed to initialize vmo; error: %d\n", status);
        return status;
    }

    zx_object_set_property(vmo_.get(), ZX_PROP_NAME, "minfs-inode", 11);

    if ((status = fs_->bc_->AttachVmo(vmo_.get(), &vmoid_)) != ZX_OK) {
        vmo_.reset();
        return status;
    }
    ReadTxn txn(fs_->bc_.get());
    uint32_t dnum_count = 0;
    uint32_t inum_count = 0;
    uint32_t dinum_count = 0;
    fs::Ticker ticker(fs_->StartTicker());
    auto get_metrics = fbl::MakeAutoCall([&]() {
        fs_->UpdateInitMetrics(dnum_count, inum_count, dinum_count, vmo_size,
                               ticker.End());
    });

    // Initialize all direct blocks
    blk_t bno;
    for (uint32_t d = 0; d < kMinfsDirect; d++) {
        if ((bno = inode_.dnum[d]) != 0) {
            fs_->ValidateBno(bno);
            dnum_count++;
            txn.Enqueue(vmoid_, d, bno + fs_->Info().dat_block, 1);
        }
    }

    // Initialize all indirect blocks
    for (uint32_t i = 0; i < kMinfsIndirect; i++) {
        blk_t ibno;
        if ((ibno = inode_.inum[i]) != 0) {
            fs_->ValidateBno(ibno);
            inum_count++;

            // Only initialize the indirect vmo if it is being used.
            if ((status = InitIndirectVmo()) != ZX_OK) {
                vmo_.reset();
                return status;
            }

            uint32_t* ientry;
            ReadIndirectVmoBlock(i, &ientry);

            for (uint32_t j = 0; j < kMinfsDirectPerIndirect; j++) {
                if ((bno = ientry[j]) != 0) {
                    fs_->ValidateBno(bno);
                    uint32_t n = kMinfsDirect + i * kMinfsDirectPerIndirect + j;
                    txn.Enqueue(vmoid_, n, bno + fs_->Info().dat_block, 1);
                }
            }
        }
    }

    // Initialize all doubly indirect blocks
    for (uint32_t i = 0; i < kMinfsDoublyIndirect; i++) {
        blk_t dibno;

        if ((dibno = inode_.dinum[i]) != 0) {
            fs_->ValidateBno(dibno);
            dinum_count++;

            // Only initialize the doubly indirect vmo if it is being used.
            if ((status = InitIndirectVmo()) != ZX_OK) {
                vmo_.reset();
                return status;
            }

            uint32_t* dientry;
            ReadIndirectVmoBlock(GetVmoOffsetForDoublyIndirect(i), &dientry);

            for (uint32_t j = 0; j < kMinfsDirectPerIndirect; j++) {
                blk_t ibno;
                if ((ibno = dientry[j]) != 0) {
                    fs_->ValidateBno(ibno);

                    // Only initialize the indirect vmo if it is being used.
                    if ((status = LoadIndirectWithinDoublyIndirect(i)) != ZX_OK) {
                        vmo_.reset();
                        return status;
                    }

                    uint32_t* ientry;
                    ReadIndirectVmoBlock(GetVmoOffsetForIndirect(i) + j, &ientry);

                    for (uint32_t k = 0; k < kMinfsDirectPerIndirect; k++) {
                        if ((bno = ientry[k]) != 0) {
                            fs_->ValidateBno(bno);
                            uint32_t n = kMinfsDirect + kMinfsIndirect * kMinfsDirectPerIndirect
                                         + j * kMinfsDirectPerIndirect + k;
                            txn.Enqueue(vmoid_, n, bno + fs_->Info().dat_block, 1);
                        }
                    }
                }
            }
        }
    }

    status = txn.Flush();
    ValidateVmoTail();
    return status;
}
#endif

zx_status_t VnodeMinfs::AllocateIndirect(WritebackWork* wb, blk_t index, IndirectArgs* args) {
    ZX_DEBUG_ASSERT(wb != nullptr);

    // *bno must not be already allocated
    ZX_DEBUG_ASSERT(args->GetBno(index) == 0);

    // allocate new indirect block
    zx_status_t status;
    blk_t bno;
    if ((status = fs_->BlockNew(wb, &bno)) != ZX_OK) {
        return status;
    }

#ifdef __Fuchsia__
    ClearIndirectVmoBlock(args->GetOffset() + index);
#else
    ClearIndirectBlock(bno);
#endif

    args->SetBno(index, bno);
    inode_.block_count++;
    return ZX_OK;
}

zx_status_t VnodeMinfs::BlockOpDirect(WritebackWork* wb, DirectArgs* params) {
    for (unsigned i = 0; i < params->GetCount(); i++) {
        blk_t bno = params->GetBno(i);
        switch (params->GetOp()) {
            case DELETE: {
                // If we found a valid block, delete it.
                if (bno) {
                    fs_->ValidateBno(bno);
                    fs_->BlockFree(wb, bno);
                    params->SetBno(i, 0);
                    inode_.block_count--;
                }
                break;
            }
            case WRITE: {
                ZX_DEBUG_ASSERT(wb != nullptr);
                if (bno == 0) {
                    zx_status_t status;
                    if ((status = fs_->BlockNew(wb, &bno)) != ZX_OK) {
                        return status;
                    }
                    inode_.block_count++;
                }

                fs_->ValidateBno(bno);
            }
            __FALLTHROUGH;
            case READ: {
                params->SetBno(i, bno);
                break;
            }
            default: {
                return ZX_ERR_NOT_SUPPORTED;
            }
        }
    }

    return ZX_OK;
}

zx_status_t VnodeMinfs::BlockOpIndirect(WritebackWork* wb, IndirectArgs* params) {
    // we should have initialized vmo before calling this method
    zx_status_t status;

#ifdef __Fuchsia__
    if (params->GetOp() == READ || params->GetOp() == WRITE) {
        validate_vmo_size(vmo_indirect_->GetVmo(), params->GetOffset() + params->GetCount());
    }
#endif

    for (unsigned i = 0; i < params->GetCount(); i++) {
        bool dirty = false;
        if (params->GetBno(i) == 0) {
            switch (params->GetOp()) {
            case DELETE:
                continue;
            case READ:
                return ZX_OK;
            case WRITE:
                if ((status = AllocateIndirect(wb, i, params)) != ZX_OK) {
                    return status;
                }
                break;
            default:
                return ZX_ERR_NOT_SUPPORTED;
            }

        }

#ifdef __Fuchsia__
        blk_t* entry;
        ReadIndirectVmoBlock(params->GetOffset() + i, &entry);
#else
        blk_t entry[kMinfsBlockSize];
        ReadIndirectBlock(params->GetBno(i), entry);
#endif

        DirectArgs direct_params = params->GetDirect(entry, i);
        if ((status = BlockOpDirect(wb, &direct_params)) != ZX_OK) {
            return status;
        }

        // only update the indirect block if an entry was deleted
        if (dirty || direct_params.IsDirty()) {
#ifdef __Fuchsia__
            wb->Enqueue(vmo_indirect_->GetVmo(), params->GetOffset() + i,
                        params->GetBno(i) + fs_->Info().dat_block, 1);
#else
            fs_->bc_->Writeblk(params->GetBno(i) + fs_->Info().dat_block, entry);
#endif
            params->SetDirty();
        }

        // We can delete the current indirect block if all direct blocks within it are deleted
        if (params->GetOp() == DELETE && direct_params.GetCount() == kMinfsDirectPerIndirect) {
            // release the direct block itself
            fs_->BlockFree(wb, params->GetBno(i));
            params->SetBno(i, 0);
            inode_.block_count--;
        }
    }

    return ZX_OK;

}

zx_status_t VnodeMinfs::BlockOpDindirect(WritebackWork* wb, DindirectArgs* params) {
    zx_status_t status;

#ifdef __Fuchsia__
    if (params->GetOp() == READ || params->GetOp() == WRITE) {
        validate_vmo_size(vmo_indirect_->GetVmo(), params->GetOffset() + params->GetCount());
    }
#endif

    // operate on doubly indirect blocks
    for (unsigned i = 0; i < params->GetCount(); i++) {
        bool dirty = false;
        if (params->GetBno(i) == 0) {
            switch (params->GetOp()) {
            case DELETE:
                continue;
            case READ:
                return ZX_OK;
            case WRITE:
                if ((status = AllocateIndirect(wb, i, params)) != ZX_OK) {
                    return status;
                }
                break;
            default:
                return ZX_ERR_NOT_SUPPORTED;
            }
        }

#ifdef __Fuchsia__
        uint32_t* dientry;
        ReadIndirectVmoBlock(GetVmoOffsetForDoublyIndirect(i), &dientry);
#else
        uint32_t dientry[kMinfsBlockSize];
        ReadIndirectBlock(params->GetBno(i), dientry);
#endif

        // operate on blocks pointed at by the entries in the indirect block
        IndirectArgs indirect_params = params->GetIndirect(dientry, i);
        if ((status = BlockOpIndirect(wb, &indirect_params)) != ZX_OK) {
            return status;
        }

        // only update the indirect block if an entry was deleted
        if (dirty || indirect_params.IsDirty()) {
#ifdef __Fuchsia__
            wb->Enqueue(vmo_indirect_->GetVmo(), params->GetOffset() + i, params->GetBno(i) +
                        fs_->Info().dat_block, 1);
#else
            fs_->bc_->Writeblk(params->GetBno(i) + fs_->Info().dat_block, dientry);
#endif
            params->SetDirty();
        }

        // We can delete the current doubly indirect block if all indirect blocks within it
        // (and direct blocks within those) are deleted
        if (params->GetOp() == DELETE && indirect_params.GetCount() == kMinfsDirectPerDindirect) {
            // release the doubly indirect block itself
            fs_->BlockFree(wb, params->GetBno(i));
            params->SetBno(i, 0);
            inode_.block_count--;
        }
    }

    return ZX_OK;
}

#ifdef __Fuchsia__
void VnodeMinfs::ReadIndirectVmoBlock(uint32_t offset, uint32_t** entry) {
    ZX_DEBUG_ASSERT(vmo_indirect_ != nullptr);
    uintptr_t addr = reinterpret_cast<uintptr_t>(vmo_indirect_->GetData());
    validate_vmo_size(vmo_indirect_->GetVmo(), offset);
    *entry = reinterpret_cast<uint32_t*>(addr + kMinfsBlockSize * offset);
}

void VnodeMinfs::ClearIndirectVmoBlock(uint32_t offset) {
    ZX_DEBUG_ASSERT(vmo_indirect_ != nullptr);
    uintptr_t addr = reinterpret_cast<uintptr_t>(vmo_indirect_->GetData());
    validate_vmo_size(vmo_indirect_->GetVmo(), offset);
    memset(reinterpret_cast<void*>(addr + kMinfsBlockSize * offset), 0, kMinfsBlockSize);
}
#else
void VnodeMinfs::ReadIndirectBlock(blk_t bno, uint32_t* entry) {
    fs_->bc_->Readblk(bno + fs_->Info().dat_block, entry);
}

void VnodeMinfs::ClearIndirectBlock(blk_t bno) {
    uint32_t data[kMinfsBlockSize];
    memset(data, 0, kMinfsBlockSize);
    fs_->bc_->Writeblk(bno + fs_->Info().dat_block, data);
}
#endif

zx_status_t VnodeMinfs::BlockOp(WritebackWork* wb, blk_op_t op, bop_params_t* boparams) {
    blk_t start = boparams->start;
    blk_t found = 0;
    bool dirty = false;
    if (found < boparams->count && start < kMinfsDirect) {
        // array starting with first direct block
        blk_t* array = &inode_.dnum[start];
        // number of direct blocks to process
        blk_t count = fbl::min(boparams->count - found, kMinfsDirect - start);
        // if bnos exist, adjust past found (should be 0)
        blk_t* bnos = boparams->bnos == nullptr ? nullptr : &boparams->bnos[found];

        DirectArgs direct_params(op, array, count, bnos);
        zx_status_t status;
        if ((status = BlockOpDirect(wb, &direct_params)) != ZX_OK) {
            return status;
        }

        found += count;
        dirty |= direct_params.IsDirty();
    }

    // for indirect blocks, adjust past the direct blocks
    if (start < kMinfsDirect) {
        start = 0;
    } else {
        start -= kMinfsDirect;
    }

    if (found < boparams->count && start < kMinfsIndirect * kMinfsDirectPerIndirect) {
        // index of indirect block, and offset of that block within indirect vmo
        blk_t ibindex = start / kMinfsDirectPerIndirect;
        // index of direct block within indirect block
        blk_t bindex = start % kMinfsDirectPerIndirect;

        // array starting with first indirect block
        blk_t* array = &inode_.inum[ibindex];
        // number of direct blocks to process within indirect blocks
        blk_t count = fbl::min(boparams->count - found,
                               kMinfsIndirect * kMinfsDirectPerIndirect - start);
        // if bnos exist, adjust past found
        blk_t* bnos = boparams->bnos == nullptr ? nullptr : &boparams->bnos[found];

        IndirectArgs indirect_params(op, array, count, bnos, bindex, ibindex);
        zx_status_t status;
        if ((status = BlockOpIndirect(wb, &indirect_params)) != ZX_OK) {
            return status;
        }

        found += count;
        dirty |= indirect_params.IsDirty();
    }

    // for doubly indirect blocks, adjust past the indirect blocks
    if (start < kMinfsIndirect * kMinfsDirectPerIndirect) {
        start = 0;
    } else {
        start -= kMinfsIndirect * kMinfsDirectPerIndirect;
    }

    if (found < boparams->count &&
        start < kMinfsDoublyIndirect * kMinfsDirectPerIndirect * kMinfsDirectPerIndirect) {
        // index of doubly indirect block
        uint32_t dibindex = start / (kMinfsDirectPerIndirect * kMinfsDirectPerIndirect);
        ZX_DEBUG_ASSERT(dibindex < kMinfsDoublyIndirect);
        start -= (dibindex * kMinfsDirectPerIndirect * kMinfsDirectPerIndirect);

        // array starting with first doubly indirect block
        blk_t* array = &inode_.dinum[dibindex];
        // number of direct blocks to process within doubly indirect blocks
        blk_t count = fbl::min(boparams->count - found,
                kMinfsDoublyIndirect * kMinfsDirectPerIndirect * kMinfsDirectPerIndirect - start);
        // if bnos exist, adjust past found
        blk_t* bnos = boparams->bnos == nullptr ? nullptr : &boparams->bnos[found];
        // index of direct block within indirect block
        blk_t bindex = start % kMinfsDirectPerIndirect;
        // offset of indirect block within indirect vmo
        blk_t ib_vmo_offset = GetVmoOffsetForIndirect(dibindex);
        // index of indirect block within doubly indirect block
        blk_t ibindex = start / kMinfsDirectPerIndirect;
        // offset of doubly indirect block within indirect vmo
        blk_t dib_vmo_offset = GetVmoOffsetForDoublyIndirect(dibindex);

        DindirectArgs dindirect_params(op, array, count, bnos, bindex, ib_vmo_offset, ibindex,
                                       dib_vmo_offset);
        zx_status_t status;
        if ((status = BlockOpDindirect(wb, &dindirect_params)) != ZX_OK) {
            return status;
        }

        found += count;
        dirty |= dindirect_params.IsDirty();
    }

    if (dirty) {
        ZX_DEBUG_ASSERT(wb != nullptr);
        InodeSync(wb, kMxFsSyncDefault);
    }

    // Return out of range if we were not able to process all blocks
    return found == boparams->count ? ZX_OK : ZX_ERR_OUT_OF_RANGE;
}

zx_status_t VnodeMinfs::BlockGet(WritebackWork* wb, blk_t n, blk_t* bno) {
#ifdef __Fuchsia__
    if (n >= kMinfsDirect) {
        zx_status_t status;
        // If the vmo_indirect_ vmo has not been created, make it now.
        if ((status = InitIndirectVmo()) != ZX_OK) {
            return status;
        }

        // Number of blocks prior to dindirect blocks
        blk_t pre_dindirect = kMinfsDirect + kMinfsDirectPerIndirect * kMinfsIndirect;
        if (n >= pre_dindirect) {
            // Index of last doubly indirect block
            blk_t dibindex = (n - pre_dindirect) / kMinfsDirectPerDindirect;
            ZX_DEBUG_ASSERT(dibindex < kMinfsDoublyIndirect);
            uint64_t vmo_size = GetVmoSizeForIndirect(dibindex);
            // Grow VMO if we need more space to fit doubly indirect blocks
            if (vmo_indirect_->GetSize() < vmo_size) {
                if ((status = vmo_indirect_->Grow(vmo_size)) != ZX_OK) {
                    return status;
                }
            }
        }
    }
#endif

    bop_params_t boparams(n, 1, bno);
    return BlockOp(wb, wb ? WRITE : READ, &boparams);
}

// Immediately stop iterating over the directory.
#define DIR_CB_DONE 0
// Access the next direntry in the directory. Offsets updated.
#define DIR_CB_NEXT 1
// Identify that the direntry record was modified. Stop iterating.
#define DIR_CB_SAVE_SYNC 2

zx_status_t VnodeMinfs::ReadExactInternal(void* data, size_t len, size_t off) {
    size_t actual;
    zx_status_t status = ReadInternal(data, len, off, &actual);
    if (status != ZX_OK) {
        return status;
    } else if (actual != len) {
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

zx_status_t VnodeMinfs::WriteExactInternal(WritebackWork* wb, const void* data,
                                           size_t len, size_t off) {
    size_t actual;
    zx_status_t status = WriteInternal(wb, data, len, off, &actual);
    if (status != ZX_OK) {
        return status;
    } else if (actual != len) {
        return ZX_ERR_IO;
    }
    InodeSync(wb, kMxFsSyncMtime);
    return ZX_OK;
}

static zx_status_t validate_dirent(minfs_dirent_t* de, size_t bytes_read, size_t off) {
    uint32_t reclen = static_cast<uint32_t>(MinfsReclen(de, off));
    if ((bytes_read < MINFS_DIRENT_SIZE) || (reclen < MINFS_DIRENT_SIZE)) {
        FS_TRACE_ERROR("vn_dir: Could not read dirent at offset: %zd\n", off);
        return ZX_ERR_IO;
    } else if ((off + reclen > kMinfsMaxDirectorySize) || (reclen & 3)) {
        FS_TRACE_ERROR("vn_dir: bad reclen %u > %u\n", reclen, kMinfsMaxDirectorySize);
        return ZX_ERR_IO;
    } else if (de->ino != 0) {
        if ((de->namelen == 0) ||
            (de->namelen > (reclen - MINFS_DIRENT_SIZE))) {
            FS_TRACE_ERROR("vn_dir: bad namelen %u / %u\n", de->namelen, reclen);
            return ZX_ERR_IO;
        }
    }
    return ZX_OK;
}

// Updates offset information to move to the next direntry in the directory.
static zx_status_t do_next_dirent(minfs_dirent_t* de, DirectoryOffset* offs) {
    offs->off_prev = offs->off;
    offs->off += MinfsReclen(de, offs->off);
    return DIR_CB_NEXT;
}

zx_status_t VnodeMinfs::DirentCallbackFind(fbl::RefPtr<VnodeMinfs> vndir, minfs_dirent_t* de,
                                           DirArgs* args, DirectoryOffset* offs) {
    if ((de->ino != 0) && fbl::StringPiece(de->name, de->namelen) == args->name) {
        args->ino = de->ino;
        args->type = de->type;
        return DIR_CB_DONE;
    } else {
        return do_next_dirent(de, offs);
    }
}

zx_status_t VnodeMinfs::CanUnlink() const {
    // directories must be empty (dirent_count == 2)
    if (IsDirectory()) {
        if (inode_.dirent_count != 2) {
            // if we have more than "." and "..", not empty, cannot unlink
            return ZX_ERR_NOT_EMPTY;
#ifdef __Fuchsia__
        } else if (IsRemote()) {
            // we cannot unlink mount points
            return ZX_ERR_UNAVAILABLE;
#endif
        }
    }
    return ZX_OK;
}

zx_status_t VnodeMinfs::UnlinkChild(WritebackWork* wb,
                                    fbl::RefPtr<VnodeMinfs> childvn,
                                    minfs_dirent_t* de, DirectoryOffset* offs) {
    // Coalesce the current dirent with the previous/next dirent, if they
    // (1) exist and (2) are free.
    size_t off_prev = offs->off_prev;
    size_t off = offs->off;
    size_t off_next = off + MinfsReclen(de, off);
    minfs_dirent_t de_prev, de_next;
    zx_status_t status;

    // Read the direntries we're considering merging with.
    // Verify they are free and small enough to merge.
    size_t coalesced_size = MinfsReclen(de, off);
    // Coalesce with "next" first, so the kMinfsReclenLast bit can easily flow
    // back to "de" and "de_prev".
    if (!(de->reclen & kMinfsReclenLast)) {
        size_t len = MINFS_DIRENT_SIZE;
        if ((status = ReadExactInternal(&de_next, len, off_next)) != ZX_OK) {
            FS_TRACE_ERROR("unlink: Failed to read next dirent\n");
            return status;
        } else if ((status = validate_dirent(&de_next, len, off_next)) != ZX_OK) {
            FS_TRACE_ERROR("unlink: Read invalid dirent\n");
            return status;
        }
        if (de_next.ino == 0) {
            coalesced_size += MinfsReclen(&de_next, off_next);
            // If the next entry *was* last, then 'de' is now last.
            de->reclen |= (de_next.reclen & kMinfsReclenLast);
        }
    }
    if (off_prev != off) {
        size_t len = MINFS_DIRENT_SIZE;
        if ((status = ReadExactInternal(&de_prev, len, off_prev)) != ZX_OK) {
            FS_TRACE_ERROR("unlink: Failed to read previous dirent\n");
            return status;
        } else if ((status = validate_dirent(&de_prev, len, off_prev)) != ZX_OK) {
            FS_TRACE_ERROR("unlink: Read invalid dirent\n");
            return status;
        }
        if (de_prev.ino == 0) {
            coalesced_size += MinfsReclen(&de_prev, off_prev);
            off = off_prev;
        }
    }

    if (!(de->reclen & kMinfsReclenLast) && (coalesced_size >= kMinfsReclenMask)) {
        // Should only be possible if the on-disk record format is corrupted
        FS_TRACE_ERROR("unlink: Corrupted direntry with impossibly large size\n");
        return ZX_ERR_IO;
    }
    de->ino = 0;
    de->reclen = static_cast<uint32_t>(coalesced_size & kMinfsReclenMask) |
        (de->reclen & kMinfsReclenLast);
    // Erase dirent (replace with 'empty' dirent)
    if ((status = WriteExactInternal(wb, de, MINFS_DIRENT_SIZE, off)) != ZX_OK) {
        return status;
    }

    if (de->reclen & kMinfsReclenLast) {
        // Truncating the directory merely removed unused space; if it fails,
        // the directory contents are still valid.
        TruncateInternal(wb, off + MINFS_DIRENT_SIZE);
    }

    inode_.dirent_count--;

    if (MinfsMagicType(childvn->inode_.magic) == kMinfsTypeDir) {
        // Child directory had '..' which pointed to parent directory
        inode_.link_count--;
    }
    childvn->RemoveInodeLink(wb);
    wb->PinVnode(fbl::move(fbl::WrapRefPtr(this)));
    wb->PinVnode(childvn);
    return DIR_CB_SAVE_SYNC;
}

void VnodeMinfs::RemoveInodeLink(WritebackWork* wb) {
    // This effectively 'unlinks' the target node without deleting the direntry
    inode_.link_count--;
    if (MinfsMagicType(inode_.magic) == kMinfsTypeDir) {
        if (inode_.link_count == 1) {
            // Directories are initialized with two links, since they point
            // to themselves via ".". Thus, when they reach "one link", they
            // are only pointed to by themselves, and should be deleted.
            inode_.link_count--;
        }
    }

    if (fd_count_ == 0 && IsUnlinked()) {
        Purge(wb);
    }

    InodeSync(wb, kMxFsSyncMtime);
}

// caller is expected to prevent unlink of "." or ".."
zx_status_t VnodeMinfs::DirentCallbackUnlink(fbl::RefPtr<VnodeMinfs> vndir, minfs_dirent_t* de,
                                             DirArgs* args, DirectoryOffset* offs) {
    if ((de->ino == 0) || fbl::StringPiece(de->name, de->namelen) != args->name) {
        return do_next_dirent(de, offs);
    }

    fbl::RefPtr<VnodeMinfs> vn;
    zx_status_t status;
    if ((status = vndir->fs_->VnodeGet(&vn, de->ino)) < 0) {
        return status;
    }

    // If a directory was requested, then only try unlinking a directory
    if ((args->type == kMinfsTypeDir) && !vn->IsDirectory()) {
        return ZX_ERR_NOT_DIR;
    }
    if ((status = vn->CanUnlink()) != ZX_OK) {
        return status;
    }
    return vndir->UnlinkChild(args->wb, fbl::move(vn), de, offs);
}

// same as unlink, but do not validate vnode
zx_status_t VnodeMinfs::DirentCallbackForceUnlink(fbl::RefPtr<VnodeMinfs> vndir, minfs_dirent_t* de,
                                                  DirArgs* args, DirectoryOffset* offs) {
    if ((de->ino == 0) || fbl::StringPiece(de->name, de->namelen) != args->name) {
        return do_next_dirent(de, offs);
    }

    fbl::RefPtr<VnodeMinfs> vn;
    zx_status_t status;
    if ((status = vndir->fs_->VnodeGet(&vn, de->ino)) < 0) {
        return status;
    }
    return vndir->UnlinkChild(args->wb, fbl::move(vn), de, offs);
}

// Given a (name, inode, type) combination:
//   - If no corresponding 'name' is found, ZX_ERR_NOT_FOUND is returned
//   - If the 'name' corresponds to a vnode, check that the target vnode:
//      - Does not have the same inode as the argument inode
//      - Is the same type as the argument 'type'
//      - Is unlinkable
//   - If the previous checks pass, then:
//      - Remove the old vnode (decrement link count by one)
//      - Replace the old vnode's position in the directory with the new inode
zx_status_t VnodeMinfs::DirentCallbackAttemptRename(fbl::RefPtr<VnodeMinfs> vndir,
                                                    minfs_dirent_t* de, DirArgs* args,
                                                    DirectoryOffset* offs) {
    if ((de->ino == 0) || fbl::StringPiece(de->name, de->namelen) != args->name) {
        return do_next_dirent(de, offs);
    }

    fbl::RefPtr<VnodeMinfs> vn;
    zx_status_t status;
    if ((status = vndir->fs_->VnodeGet(&vn, de->ino)) < 0) {
        return status;
    } else if (args->ino == vn->ino_) {
        // cannot rename node to itself
        return ZX_ERR_BAD_STATE;
    } else if (args->type != de->type) {
        // cannot rename directory to file (or vice versa)
        return ZX_ERR_BAD_STATE;
    } else if ((status = vn->CanUnlink()) != ZX_OK) {
        // if we cannot unlink the target, we cannot rename the target
        return status;
    }

    // If we are renaming ON TOP of a directory, then we can skip
    // updating the parent link count -- the old directory had a ".." entry to
    // the parent (link count of 1), but the new directory will ALSO have a ".."
    // entry, making the rename operation idempotent w.r.t. the parent link
    // count.
    vn->RemoveInodeLink(args->wb);

    de->ino = args->ino;
    status = vndir->WriteExactInternal(args->wb, de, DirentSize(de->namelen), offs->off);
    if (status != ZX_OK) {
        return status;
    }

    args->wb->PinVnode(vn);
    args->wb->PinVnode(vndir);
    return DIR_CB_SAVE_SYNC;
}

zx_status_t VnodeMinfs::DirentCallbackUpdateInode(fbl::RefPtr<VnodeMinfs> vndir, minfs_dirent_t* de,
                                                  DirArgs* args, DirectoryOffset* offs) {
    if ((de->ino == 0) || fbl::StringPiece(de->name, de->namelen) != args->name) {
        return do_next_dirent(de, offs);
    }

    de->ino = args->ino;
    zx_status_t status = vndir->WriteExactInternal(args->wb, de,
                                                   DirentSize(de->namelen),
                                                   offs->off);
    if (status != ZX_OK) {
        return status;
    }
    args->wb->PinVnode(vndir);
    return DIR_CB_SAVE_SYNC;
}

zx_status_t VnodeMinfs::DirentCallbackAppend(fbl::RefPtr<VnodeMinfs> vndir, minfs_dirent_t* de,
                                             DirArgs* args, DirectoryOffset* offs) {
    auto add_dirent = [](fbl::RefPtr<VnodeMinfs> vndir, minfs_dirent_t* de, DirArgs* args,
                         size_t off) {
        de->ino = args->ino;
        de->type = static_cast<uint8_t>(args->type);
        de->namelen = static_cast<uint8_t>(args->name.length());
        memcpy(de->name, args->name.data(), de->namelen);
        zx_status_t status = vndir->WriteExactInternal(args->wb, de, DirentSize(de->namelen),
                                                       off);
        if (status != ZX_OK) {
            return status;
        }
        vndir->inode_.dirent_count++;
        if (args->type == kMinfsTypeDir) {
            // Child directory has '..' which will point to parent directory
            vndir->inode_.link_count++;
        }
        args->wb->PinVnode(fbl::move(vndir));
        return DIR_CB_SAVE_SYNC;
    };

    uint32_t reclen = static_cast<uint32_t>(MinfsReclen(de, offs->off));
    if (de->ino == 0) {
        // empty entry, do we fit?
        if (args->reclen > reclen) {
            return do_next_dirent(de, offs);
        }
        return add_dirent(fbl::move(vndir), de, args, offs->off);
    } else {
        // filled entry, can we sub-divide?
        uint32_t size = static_cast<uint32_t>(DirentSize(de->namelen));
        if (size > reclen) {
            FS_TRACE_ERROR("bad reclen (smaller than dirent) %u < %u\n", reclen, size);
            return ZX_ERR_IO;
        }
        uint32_t extra = reclen - size;
        if (extra < args->reclen) {
            return do_next_dirent(de, offs);
        }
        // shrink existing entry
        bool was_last_record = de->reclen & kMinfsReclenLast;
        de->reclen = size;
        zx_status_t status = vndir->WriteExactInternal(args->wb, de,
                                                       DirentSize(de->namelen),
                                                       offs->off);
        if (status != ZX_OK) {
            return status;
        }
        offs->off += size;
        // create new entry in the remaining space
        char data[kMinfsMaxDirentSize];
        de = (minfs_dirent_t*) data;
        de->reclen = extra | (was_last_record ? kMinfsReclenLast : 0);
        return add_dirent(fbl::move(vndir), de, args, offs->off);
    }
}

// Calls a callback 'func' on all direntries in a directory 'vn' with the
// provided arguments, reacting to the return code of the callback.
//
// When 'func' is called, it receives a few arguments:
//  'vndir': The directory on which the callback is operating
//  'de': A pointer the start of a single dirent.
//        Only DirentSize(de->namelen) bytes are guaranteed to exist in
//        memory from this starting pointer.
//  'args': Additional arguments plumbed through ForEachDirent
//  'offs': Offset info about where in the directory this direntry is located.
//          Since 'func' may create / remove surrounding dirents, it is responsible for
//          updating the offset information to access the next dirent.
zx_status_t VnodeMinfs::ForEachDirent(DirArgs* args, const DirentCallback func) {
    char data[kMinfsMaxDirentSize];
    minfs_dirent_t* de = (minfs_dirent_t*) data;
    DirectoryOffset offs = {
        .off = 0,
        .off_prev = 0,
    };
    while (offs.off + MINFS_DIRENT_SIZE < kMinfsMaxDirectorySize) {
        xprintf("Reading dirent at offset %zd\n", offs.off);
        size_t r;
        zx_status_t status = ReadInternal(data, kMinfsMaxDirentSize, offs.off, &r);
        if (status != ZX_OK) {
            return status;
        } else if ((status = validate_dirent(de, r, offs.off)) != ZX_OK) {
            return status;
        }

        switch ((status = func(fbl::RefPtr<VnodeMinfs>(this), de, args, &offs))) {
        case DIR_CB_NEXT:
            break;
        case DIR_CB_SAVE_SYNC:
            inode_.seq_num++;
            InodeSync(args->wb, kMxFsSyncMtime);
            args->wb->PinVnode(fbl::move(fbl::WrapRefPtr(this)));
            return ZX_OK;
        case DIR_CB_DONE:
        default:
            return status;
        }
    }
    return ZX_ERR_NOT_FOUND;
}

void VnodeMinfs::fbl_recycle() {
    ZX_DEBUG_ASSERT(fd_count_ == 0);
    if (!IsUnlinked()) {
        // If this node has not been purged already, remove it from the
        // hash map. If it has been purged; it will already be absent
        // from the map (and may have already been replaced with a new
        // node, if the inode has been re-used).
        fs_->VnodeRelease(this);
    }
    delete this;
}

VnodeMinfs::~VnodeMinfs() {
#ifdef __Fuchsia__
    // Detach the vmoids from the underlying block device,
    // so the underlying VMO may be released.
    size_t request_count = 0;
    block_fifo_request_t request[2];
    if (vmo_.is_valid()) {
        request[request_count].group = fs_->bc_->BlockGroupID();
        request[request_count].vmoid = vmoid_;
        request[request_count].opcode = BLOCKIO_CLOSE_VMO;
        request_count++;
    }
    if (vmo_indirect_ != nullptr) {
        request[request_count].group = fs_->bc_->BlockGroupID();
        request[request_count].vmoid = vmoid_indirect_;
        request[request_count].opcode = BLOCKIO_CLOSE_VMO;
        request_count++;
    }
    if (request_count) {
        fs_->bc_->Txn(&request[0], request_count);
    }
#endif
}

zx_status_t VnodeMinfs::ValidateFlags(uint32_t flags) {
    xprintf("VnodeMinfs::ValidateFlags(0x%x) vn=%p(#%u)\n", flags, this, ino_);
    if ((flags & ZX_FS_FLAG_DIRECTORY) && !IsDirectory()) {
        return ZX_ERR_NOT_DIR;
    }

    if ((flags & ZX_FS_RIGHT_WRITABLE) && IsDirectory()) {
        return ZX_ERR_NOT_FILE;
    }
    return ZX_OK;
}

zx_status_t VnodeMinfs::Open(uint32_t flags, fbl::RefPtr<Vnode>* out_redirect) {
    fd_count_++;
    return ZX_OK;
}

void VnodeMinfs::Purge(WritebackWork* wb) {
    ZX_DEBUG_ASSERT(fd_count_ == 0);
    ZX_DEBUG_ASSERT(IsUnlinked());
    fs_->VnodeRelease(this);
#ifdef __Fuchsia__
    // TODO(smklein): Only init indirect vmo if it's needed
    if (InitIndirectVmo() == ZX_OK) {
        fs_->InoFree(this, wb);
    } else {
        fprintf(stderr, "minfs: Failed to Init Indirect VMO while purging %u\n", ino_);
    }
#else
    fs_->InoFree(this, wb);
#endif
}

zx_status_t VnodeMinfs::Close() {
    ZX_DEBUG_ASSERT_MSG(fd_count_ > 0, "Closing ino with no fds open");
    fd_count_--;

    if (fd_count_ == 0 && IsUnlinked()) {
        fbl::unique_ptr<WritebackWork> wb;
        fs_->CreateWork(&wb);
        Purge(wb.get());
        fs_->EnqueueWork(fbl::move(wb));
    }
    return ZX_OK;
}

zx_status_t VnodeMinfs::Read(void* data, size_t len, size_t off, size_t* out_actual) {
    TRACE_DURATION("minfs", "VnodeMinfs::Read", "ino", ino_, "len", len, "off", off);
    ZX_DEBUG_ASSERT_MSG(fd_count_ > 0, "Reading from ino with no fds open");
    xprintf("minfs_read() vn=%p(#%u) len=%zd off=%zd\n", this, ino_, len, off);
    if (IsDirectory()) {
        return ZX_ERR_NOT_FILE;
    }

    fs::Ticker ticker(fs_->StartTicker());
    auto get_metrics = fbl::MakeAutoCall([&ticker, &out_actual, this]() {
        fs_->UpdateReadMetrics(*out_actual, ticker.End());
    });

    zx_status_t status = ReadInternal(data, len, off, out_actual);
    if (status != ZX_OK) {
        return status;
    }
    return ZX_OK;
}

// Internal read. Usable on directories.
zx_status_t VnodeMinfs::ReadInternal(void* data, size_t len, size_t off, size_t* actual) {
    // clip to EOF
    if (off >= inode_.size) {
        *actual = 0;
        return ZX_OK;
    }
    if (len > (inode_.size - off)) {
        len = inode_.size - off;
    }

    zx_status_t status;
#ifdef __Fuchsia__
    if ((status = InitVmo()) != ZX_OK) {
        return status;
    } else if ((status = vmo_.read(data, off, len)) != ZX_OK) {
        return status;
    } else {
        *actual = len;
    }
#else
    void* start = data;
    uint32_t n = off / kMinfsBlockSize;
    size_t adjust = off % kMinfsBlockSize;

    while ((len > 0) && (n < kMinfsMaxFileBlock)) {
        size_t xfer;
        if (len > (kMinfsBlockSize - adjust)) {
            xfer = kMinfsBlockSize - adjust;
        } else {
            xfer = len;
        }

        blk_t bno;
        if ((status = BlockGet(nullptr, n, &bno)) != ZX_OK) {
            return status;
        }
        if (bno != 0) {
            char bdata[kMinfsBlockSize];
            if (fs_->ReadDat(bno, bdata)) {
                FS_TRACE_ERROR("minfs: Failed to read data block %u\n", bno);
                return ZX_ERR_IO;
            }
            memcpy(data, bdata + adjust, xfer);
        } else {
            // If the block is not allocated, just read zeros
            memset(data, 0, xfer);
        }

        adjust = 0;
        len -= xfer;
        data = (void*)((uintptr_t)data + xfer);
        n++;
    }
    *actual = (uintptr_t)data - (uintptr_t)start;
#endif
    return ZX_OK;
}

zx_status_t VnodeMinfs::Write(const void* data, size_t len, size_t offset,
                              size_t* out_actual) {
    TRACE_DURATION("minfs", "VnodeMinfs::Write", "ino", ino_, "len", len, "off", offset);
    ZX_DEBUG_ASSERT_MSG(fd_count_ > 0, "Writing to ino with no fds open");
    xprintf("minfs_write() vn=%p(#%u) len=%zd off=%zd\n", this, ino_, len, offset);
    if (IsDirectory()) {
        return ZX_ERR_NOT_FILE;
    }

    *out_actual = 0;
    fs::Ticker ticker(fs_->StartTicker());
    auto get_metrics = fbl::MakeAutoCall([&ticker, &out_actual, this]() {
        fs_->UpdateWriteMetrics(*out_actual, ticker.End());
    });

    zx_status_t status;
    fbl::unique_ptr<WritebackWork> wb;
    if ((status = fs_->CreateWork(&wb)) != ZX_OK) {
        return status;
    }

    status = WriteInternal(wb.get(), data, len, offset, out_actual);
    if (status != ZX_OK) {
        return status;
    }
    if (*out_actual != 0) {
        InodeSync(wb.get(), kMxFsSyncMtime);  // Successful writes updates mtime
        wb->PinVnode(fbl::move(fbl::WrapRefPtr(this)));
        fs_->EnqueueWork(fbl::move(wb));
    }
    return ZX_OK;
}

zx_status_t VnodeMinfs::Append(const void* data, size_t len, size_t* out_end,
                               size_t* out_actual) {
    zx_status_t status = Write(data, len, inode_.size, out_actual);
    *out_end = inode_.size;
    return status;
}

// Internal write. Usable on directories.
zx_status_t VnodeMinfs::WriteInternal(WritebackWork* wb, const void* data,
                                      size_t len, size_t off, size_t* actual) {
    if (len == 0) {
        *actual = 0;
        return ZX_OK;
    }

    zx_status_t status;
#ifdef __Fuchsia__
    if ((status = InitVmo()) != ZX_OK) {
        return status;
    }
#else
    size_t max_size = off + len;
#endif
    const void* const start = data;
    uint32_t n = static_cast<uint32_t>(off / kMinfsBlockSize);
    size_t adjust = off % kMinfsBlockSize;

    while ((len > 0) && (n < kMinfsMaxFileBlock)) {
        size_t xfer;
        if (len > (kMinfsBlockSize - adjust)) {
            xfer = kMinfsBlockSize - adjust;
        } else {
            xfer = len;
        }

#ifdef __Fuchsia__
        size_t xfer_off = n * kMinfsBlockSize + adjust;
        if ((xfer_off + xfer) > inode_.size) {
            size_t new_size = fbl::round_up(xfer_off + xfer, kMinfsBlockSize);
            ZX_DEBUG_ASSERT(new_size >= inode_.size); // Overflow.
            if ((status = vmo_.set_size(new_size)) != ZX_OK) {
                goto done;
            }
        }

        // Update this block of the in-memory VMO
        if ((status = vmo_.write(data, xfer_off, xfer)) != ZX_OK) {
            goto done;
        }

        // Update this block on-disk
        blk_t bno;
        if ((status = BlockGet(wb, n, &bno))) {
            goto done;
        }
        ZX_DEBUG_ASSERT(bno != 0);
        wb->Enqueue(vmo_.get(), n, bno + fs_->Info().dat_block, 1);
#else
        blk_t bno;
        if ((status = BlockGet(wb, n, &bno))) {
            goto done;
        }
        ZX_DEBUG_ASSERT(bno != 0);
        char wdata[kMinfsBlockSize];
        if (fs_->bc_->Readblk(bno + fs_->Info().dat_block, wdata)) {
            goto done;
        }
        memcpy(wdata + adjust, data, xfer);
        if (len < kMinfsBlockSize && max_size >= inode_.size) {
            memset(wdata + adjust + xfer, 0, kMinfsBlockSize - (adjust + xfer));
        }
        if (fs_->bc_->Writeblk(bno + fs_->Info().dat_block, wdata)) {
            goto done;
        }
#endif

        adjust = 0;
        len -= xfer;
        data = (void*)((uintptr_t)(data) + xfer);
        n++;
    }

done:
    len = (uintptr_t)data - (uintptr_t)start;
    if (len == 0) {
        // If more than zero bytes were requested, but zero bytes were written,
        // return an error explicitly (rather than zero).
        if (off >= kMinfsMaxFileSize) {
            return ZX_ERR_FILE_BIG;
        }

        return ZX_ERR_NO_SPACE;
    }

    if ((off + len) > inode_.size) {
        inode_.size = static_cast<uint32_t>(off + len);
    }

    *actual = len;
    ValidateVmoTail();
    return ZX_OK;
}

zx_status_t VnodeMinfs::Lookup(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name) {
    TRACE_DURATION("minfs", "VnodeMinfs::Lookup", "name", name);
    ZX_DEBUG_ASSERT(fs::vfs_valid_name(name));

    if (!IsDirectory()) {
        FS_TRACE_ERROR("not directory\n");
        return ZX_ERR_NOT_SUPPORTED;
    }

    return LookupInternal(out, name);
}

zx_status_t VnodeMinfs::LookupInternal(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name) {
    DirArgs args = DirArgs();
    args.name = name;
    zx_status_t status;
    bool success = false;
    fs::Ticker ticker(fs_->StartTicker());
    auto get_metrics = fbl::MakeAutoCall([&ticker, &success, this]() {
        fs_->UpdateLookupMetrics(success, ticker.End());
    });
    if ((status = ForEachDirent(&args, DirentCallbackFind)) < 0) {
        return status;
    }
    fbl::RefPtr<VnodeMinfs> vn;
    if ((status = fs_->VnodeGet(&vn, args.ino)) < 0) {
        return status;
    }
    *out = fbl::move(vn);
    success = (status == ZX_OK);
    return status;
}

zx_status_t VnodeMinfs::Getattr(vnattr_t* a) {
    xprintf("minfs_getattr() vn=%p(#%u)\n", this, ino_);
    a->mode = DTYPE_TO_VTYPE(MinfsMagicType(inode_.magic)) |
            V_IRUSR | V_IWUSR | V_IRGRP | V_IROTH;
    a->inode = ino_;
    a->size = inode_.size;
    a->blksize = kMinfsBlockSize;
    a->blkcount = inode_.block_count * (kMinfsBlockSize / VNATTR_BLKSIZE);
    a->nlink = inode_.link_count;
    a->create_time = inode_.create_time;
    a->modify_time = inode_.modify_time;
    return ZX_OK;
}

zx_status_t VnodeMinfs::Setattr(const vnattr_t* a) {
    int dirty = 0;
    xprintf("minfs_setattr() vn=%p(#%u)\n", this, ino_);
    if ((a->valid & ~(ATTR_CTIME|ATTR_MTIME)) != 0) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    if ((a->valid & ATTR_CTIME) != 0) {
        inode_.create_time = a->create_time;
        dirty = 1;
    }
    if ((a->valid & ATTR_MTIME) != 0) {
        inode_.modify_time = a->modify_time;
        dirty = 1;
    }
    if (dirty) {
        // write to disk, but don't overwrite the time
        zx_status_t status;
        fbl::unique_ptr<WritebackWork> wb;
        if ((status = fs_->CreateWork(&wb)) != ZX_OK) {
            return status;
        }
        InodeSync(wb.get(), kMxFsSyncDefault);
        wb->PinVnode(fbl::move(fbl::WrapRefPtr(this)));
        fs_->EnqueueWork(fbl::move(wb));
    }
    return ZX_OK;
}

typedef struct dircookie {
    size_t off;        // Offset into directory
    uint32_t reserved; // Unused
    uint32_t seqno;    // inode seq no
} dircookie_t;

static_assert(sizeof(dircookie_t) <= sizeof(fs::vdircookie_t),
              "MinFS dircookie too large to fit in IO state");

zx_status_t VnodeMinfs::Readdir(fs::vdircookie_t* cookie, void* dirents, size_t len,
                                size_t* out_actual) {
    TRACE_DURATION("minfs", "VnodeMinfs::Readdir");
    xprintf("minfs_readdir() vn=%p(#%u) cookie=%p len=%zd\n", this, ino_, cookie, len);
    dircookie_t* dc = reinterpret_cast<dircookie_t*>(cookie);
    fs::DirentFiller df(dirents, len);

    if (!IsDirectory()) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    size_t off = dc->off;
    size_t r;
    char data[kMinfsMaxDirentSize];
    minfs_dirent_t* de = (minfs_dirent_t*) data;

    if (off != 0 && dc->seqno != inode_.seq_num) {
        // The offset *might* be invalid, if we called Readdir after a directory
        // has been modified. In this case, we need to re-read the directory
        // until we get to the direntry at or after the previously identified offset.

        size_t off_recovered = 0;
        while (off_recovered < off) {
            if (off_recovered + MINFS_DIRENT_SIZE >= kMinfsMaxDirectorySize) {
                FS_TRACE_ERROR("minfs: Readdir: Corrupt dirent; dirent reclen too large\n");
                goto fail;
            }
            zx_status_t status = ReadInternal(de, kMinfsMaxDirentSize, off_recovered, &r);
            if ((status != ZX_OK) || (validate_dirent(de, r, off_recovered) != ZX_OK)) {
                FS_TRACE_ERROR("minfs: Readdir: Corrupt dirent unreadable/failed validation\n");
                goto fail;
            }
            off_recovered += MinfsReclen(de, off_recovered);
        }
        off = off_recovered;
    }

    while (off + MINFS_DIRENT_SIZE < kMinfsMaxDirectorySize) {
        zx_status_t status = ReadInternal(de, kMinfsMaxDirentSize, off, &r);
        if (status != ZX_OK) {
            FS_TRACE_ERROR("minfs: Readdir: Unreadable dirent\n");
            goto fail;
        } else if (validate_dirent(de, r, off) != ZX_OK) {
            FS_TRACE_ERROR("minfs: Readdir: Corrupt dirent failed validation\n");
            goto fail;
        }

        fbl::StringPiece name(de->name, de->namelen);

        if (de->ino && name != "..") {
            zx_status_t status;
            if ((status = df.Next(name, de->type)) != ZX_OK) {
                // no more space
                goto done;
            }
        }

        off += MinfsReclen(de, off);
    }

done:
    // save our place in the dircookie
    dc->off = off;
    dc->seqno = inode_.seq_num;
    *out_actual = df.BytesFilled();
    ZX_DEBUG_ASSERT(*out_actual <= len); // Otherwise, we're overflowing the input buffer.
    return ZX_OK;

fail:
    dc->off = 0;
    return ZX_ERR_IO;
}

#ifdef __Fuchsia__
VnodeMinfs::VnodeMinfs(Minfs* fs) :
    fs_(fs), vmo_(ZX_HANDLE_INVALID), vmo_indirect_(nullptr) {}

void VnodeMinfs::Notify(fbl::StringPiece name, unsigned event) { watcher_.Notify(name, event); }
zx_status_t VnodeMinfs::WatchDir(fs::Vfs* vfs, const vfs_watch_dir_t* cmd) {
    return watcher_.WatchDir(vfs, this, cmd);
}

bool VnodeMinfs::IsRemote() const { return remoter_.IsRemote(); }
zx::channel VnodeMinfs::DetachRemote() { return remoter_.DetachRemote(); }
zx_handle_t VnodeMinfs::GetRemote() const { return remoter_.GetRemote(); }
void VnodeMinfs::SetRemote(zx::channel remote) { return remoter_.SetRemote(fbl::move(remote)); }

#else
VnodeMinfs::VnodeMinfs(Minfs* fs) : fs_(fs) {}
#endif

zx_status_t VnodeMinfs::Allocate(Minfs* fs, uint32_t type, fbl::RefPtr<VnodeMinfs>* out) {
    fbl::AllocChecker ac;
    *out = fbl::AdoptRef(new (&ac) VnodeMinfs(fs));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    memset(&(*out)->inode_, 0, sizeof((*out)->inode_));
    (*out)->inode_.magic = MinfsMagic(type);
    (*out)->inode_.create_time = (*out)->inode_.modify_time = minfs_gettime_utc();
    (*out)->inode_.link_count = (type == kMinfsTypeDir ? 2 : 1);
    return ZX_OK;
}

zx_status_t VnodeMinfs::Recreate(Minfs* fs, ino_t ino, fbl::RefPtr<VnodeMinfs>* out) {
    fbl::AllocChecker ac;
    *out = fbl::AdoptRef(new (&ac) VnodeMinfs(fs));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    fs->InodeLoad(ino, &(*out)->inode_);
    (*out)->ino_ = ino;
    return ZX_OK;
}

zx_status_t VnodeMinfs::Create(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name, uint32_t mode) {
    TRACE_DURATION("minfs", "VnodeMinfs::Create", "name", name);
    ZX_DEBUG_ASSERT(fs::vfs_valid_name(name));

    bool success = false;
    fs::Ticker ticker(fs_->StartTicker());
    auto get_metrics = fbl::MakeAutoCall([&ticker, &success, this]() {
        fs_->UpdateCreateMetrics(success, ticker.End());
    });

    if (!IsDirectory()) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    if (IsUnlinked()) {
        return ZX_ERR_BAD_STATE;
    }

    DirArgs args = DirArgs();
    args.name = name;
    // ensure file does not exist
    zx_status_t status;
    if ((status = ForEachDirent(&args, DirentCallbackFind)) != ZX_ERR_NOT_FOUND) {
        return ZX_ERR_ALREADY_EXISTS;
    }

    // creating a directory?
    uint32_t type = S_ISDIR(mode) ? kMinfsTypeDir : kMinfsTypeFile;

    fbl::unique_ptr<WritebackWork> wb;
    if ((status = fs_->CreateWork(&wb)) != ZX_OK) {
        return status;
    }

    // mint a new inode and vnode for it
    fbl::RefPtr<VnodeMinfs> vn;
    if ((status = fs_->VnodeNew(wb.get(), &vn, type)) < 0) {
        return status;
    }

    // If the new node is a directory, fill it with '.' and '..'.
    if (type == kMinfsTypeDir) {
        char bdata[DirentSize(1) + DirentSize(2)];
        minfs_dir_init(bdata, vn->ino_, ino_);
        size_t expected = DirentSize(1) + DirentSize(2);
        if ((status = vn->WriteExactInternal(wb.get(), bdata, expected, 0)) != ZX_OK) {
            FS_TRACE_ERROR("minfs: Create: Failed to initialize empty directory: %d\n", status);
            return ZX_ERR_IO;
        }
        vn->inode_.dirent_count = 2;
        vn->InodeSync(wb.get(), kMxFsSyncDefault);
    }

    // add directory entry for the new child node
    args.ino = vn->ino_;
    args.type = type;
    args.reclen = static_cast<uint32_t>(DirentSize(static_cast<uint8_t>(name.length())));
    args.wb = wb.get();
    if ((status = ForEachDirent(&args, DirentCallbackAppend)) < 0) {
        return status;
    }

    wb->PinVnode(fbl::move(fbl::WrapRefPtr(this)));
    wb->PinVnode(vn);
    fs_->EnqueueWork(fbl::move(wb));

    vn->fd_count_ = 1;
    *out = fbl::move(vn);
    success = (status == ZX_OK);
    return status;
}

constexpr const char kFsName[] = "minfs";

zx_status_t VnodeMinfs::Ioctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                              size_t out_len, size_t* out_actual) {
    switch (op) {
        case IOCTL_VFS_QUERY_FS: {
            if (out_len < (sizeof(vfs_query_info_t) + strlen(kFsName))) {
                return ZX_ERR_INVALID_ARGS;
            }

            vfs_query_info_t* info = static_cast<vfs_query_info_t*>(out_buf);
            memset(info, 0, sizeof(*info));
            info->block_size = kMinfsBlockSize;
            info->max_filename_size = kMinfsMaxNameSize;
            info->fs_type = VFS_TYPE_MINFS;
#ifdef __Fuchsia__
            info->fs_id = fs_->GetFsId();
#endif
            info->total_bytes = fs_->Info().block_count * fs_->Info().block_size;
            info->used_bytes = fs_->Info().alloc_block_count * fs_->Info().block_size;
            info->total_nodes = fs_->Info().inode_count;
            info->used_nodes = fs_->Info().alloc_inode_count;
            memcpy(info->name, kFsName, strlen(kFsName));
            *out_actual = sizeof(vfs_query_info_t) + strlen(kFsName);
            return ZX_OK;
        }
#ifdef __Fuchsia__
        case IOCTL_VFS_GET_DEVICE_PATH: {
            ssize_t len = fs_->bc_->GetDevicePath(static_cast<char*>(out_buf), out_len);

            if ((ssize_t)out_len < len) {
                return ZX_ERR_INVALID_ARGS;
            }
            if (len >= 0) {
                *out_actual = len;
            }
            return len > 0 ? ZX_OK : static_cast<zx_status_t>(len);
        }
#endif
        default: {
            return ZX_ERR_NOT_SUPPORTED;
        }
    }
}

zx_status_t VnodeMinfs::Unlink(fbl::StringPiece name, bool must_be_dir) {
    TRACE_DURATION("minfs", "VnodeMinfs::Unlink", "name", name);
    ZX_DEBUG_ASSERT(fs::vfs_valid_name(name));
    bool success = false;
    fs::Ticker ticker(fs_->StartTicker());
    auto get_metrics = fbl::MakeAutoCall([&ticker, &success, this]() {
        fs_->UpdateUnlinkMetrics(success, ticker.End());
    });

    if (!IsDirectory()) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    zx_status_t status;
    fbl::unique_ptr<WritebackWork> wb;
    if ((status = fs_->CreateWork(&wb)) != ZX_OK) {
        return status;
    }
    DirArgs args = DirArgs();
    args.name = name;
    args.type = must_be_dir ? kMinfsTypeDir : 0;
    args.wb = wb.get();
    status = ForEachDirent(&args, DirentCallbackUnlink);
    if (status == ZX_OK) {
        wb->PinVnode(fbl::move(fbl::WrapRefPtr(this)));
        fs_->EnqueueWork(fbl::move(wb));
    }
    success = (status == ZX_OK);
    return status;
}

zx_status_t VnodeMinfs::Truncate(size_t len) {
    TRACE_DURATION("minfs", "VnodeMinfs::Truncate");
    if (IsDirectory()) {
        return ZX_ERR_NOT_FILE;
    }

    fs::Ticker ticker(fs_->StartTicker());
    auto get_metrics = fbl::MakeAutoCall([&ticker, this] {
        fs_->UpdateTruncateMetrics(ticker.End());
    });

    zx_status_t status;
    fbl::unique_ptr<WritebackWork> wb;
    if ((status = fs_->CreateWork(&wb)) != ZX_OK) {
        return status;
    }
    status = TruncateInternal(wb.get(), len);
    if (status == ZX_OK) {
        // Successful truncates update inode
        InodeSync(wb.get(), kMxFsSyncMtime);
    }
    wb->PinVnode(fbl::move(fbl::WrapRefPtr(this)));
    fs_->EnqueueWork(fbl::move(wb));
    return status;
}

zx_status_t VnodeMinfs::TruncateInternal(WritebackWork* wb, size_t len) {
    zx_status_t r = 0;
#ifdef __Fuchsia__
    // TODO(smklein): We should only init up to 'len'; no need
    // to read in the portion of a large file we plan on deleting.
    if ((r = InitVmo()) != ZX_OK) {
        FS_TRACE_ERROR("minfs: Truncate failed to initialize VMO: %d\n", r);
        return ZX_ERR_IO;
    }
#endif

    if (len < inode_.size) {
        // Truncate should make the file shorter
        blk_t bno = inode_.size / kMinfsBlockSize;
        blk_t trunc_bno = static_cast<blk_t>(len / kMinfsBlockSize);

        // Truncate to the nearest block
        if (trunc_bno <= bno) {
            blk_t start_bno = static_cast<blk_t>((len % kMinfsBlockSize == 0) ?
                                                 trunc_bno : trunc_bno + 1);
            if ((r = BlocksShrink(wb, start_bno)) < 0) {
                return r;
            }

            if (start_bno * kMinfsBlockSize < inode_.size) {
                inode_.size = start_bno * kMinfsBlockSize;
            }
        }

        // Write zeroes to the rest of the remaining block, if it exists
        if (len < inode_.size) {
            char bdata[kMinfsBlockSize];
            blk_t rel_bno = static_cast<blk_t>(len / kMinfsBlockSize);
            if ((r = BlockGet(nullptr, rel_bno, &bno)) != ZX_OK) {
                FS_TRACE_ERROR("minfs: Truncate failed to get block %u of file: %d\n", rel_bno, r);
                return ZX_ERR_IO;
            }
            if (bno != 0) {
                size_t adjust = len % kMinfsBlockSize;
#ifdef __Fuchsia__
                if ((r = vmo_.read(bdata, len - adjust, adjust)) != ZX_OK) {
                    FS_TRACE_ERROR("minfs: Truncate failed to read last block: %d\n", r);
                    return ZX_ERR_IO;
                }
                memset(bdata + adjust, 0, kMinfsBlockSize - adjust);

                if ((r = vmo_.write(bdata, len - adjust, kMinfsBlockSize)) != ZX_OK) {
                    FS_TRACE_ERROR("minfs: Truncate failed to write last block: %d\n", r);
                    return ZX_ERR_IO;
                }
                wb->Enqueue(vmo_.get(), rel_bno, bno + fs_->Info().dat_block, 1);
#else
                if (fs_->bc_->Readblk(bno + fs_->Info().dat_block, bdata)) {
                    return ZX_ERR_IO;
                }
                memset(bdata + adjust, 0, kMinfsBlockSize - adjust);
                if (fs_->bc_->Writeblk(bno + fs_->Info().dat_block, bdata)) {
                    return ZX_ERR_IO;
                }
#endif
            }
        }
    } else if (len > inode_.size) {
        // Truncate should make the file longer, filled with zeroes.
        if (kMinfsMaxFileSize < len) {
            return ZX_ERR_INVALID_ARGS;
        }
    } else {
        return ZX_OK;
    }

    inode_.size = static_cast<uint32_t>(len);
#ifdef __Fuchsia__
    if ((r = vmo_.set_size(fbl::round_up(len, kMinfsBlockSize))) != ZX_OK) {
        return r;
    }
#endif

    ValidateVmoTail();
    return ZX_OK;
}

// Verify that the 'newdir' inode is not a subdirectory of the source.
zx_status_t VnodeMinfs::CheckNotSubdirectory(fbl::RefPtr<VnodeMinfs> newdir) {
    fbl::RefPtr<VnodeMinfs> vn = newdir;
    zx_status_t status = ZX_OK;
    while (vn->ino_ != kMinfsRootIno) {
        if (vn->ino_ == ino_) {
            status = ZX_ERR_INVALID_ARGS;
            break;
        }

        fbl::RefPtr<fs::Vnode> out = nullptr;
        if ((status = vn->LookupInternal(&out, "..")) < 0) {
            break;
        }
        vn = fbl::RefPtr<VnodeMinfs>::Downcast(out);
    }
    return status;
}

zx_status_t VnodeMinfs::Rename(fbl::RefPtr<fs::Vnode> _newdir, fbl::StringPiece oldname,
                               fbl::StringPiece newname, bool src_must_be_dir,
                               bool dst_must_be_dir) {
    TRACE_DURATION("minfs", "VnodeMinfs::Rename", "src", oldname, "dst", newname);
    bool success = false;
    fs::Ticker ticker(fs_->StartTicker());
    auto get_metrics = fbl::MakeAutoCall([&ticker, &success, this](){
        fs_->UpdateRenameMetrics(success, ticker.End());
    });

    auto newdir = fbl::RefPtr<VnodeMinfs>::Downcast(_newdir);
    ZX_DEBUG_ASSERT(fs::vfs_valid_name(oldname));
    ZX_DEBUG_ASSERT(fs::vfs_valid_name(newname));

    // ensure that the vnodes containing oldname and newname are directories
    if (!(IsDirectory() && newdir->IsDirectory())) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status;
    fbl::RefPtr<VnodeMinfs> oldvn = nullptr;
    // acquire the 'oldname' node (it must exist)
    DirArgs args = DirArgs();
    args.name = oldname;
    if ((status = ForEachDirent(&args, DirentCallbackFind)) < 0) {
        return status;
    } else if ((status = fs_->VnodeGet(&oldvn, args.ino)) < 0) {
        return status;
    } else if ((status = oldvn->CheckNotSubdirectory(newdir)) < 0) {
        return status;
    }

    // If either the 'src' or 'dst' must be directories, BOTH of them must be directories.
    if (!oldvn->IsDirectory() && (src_must_be_dir || dst_must_be_dir)) {
        return ZX_ERR_NOT_DIR;
    } else if ((newdir->ino_ == ino_) && (oldname == newname)) {
        // Renaming a file or directory to itself?
        // Shortcut success case.
        success = true;
        return ZX_OK;
    }

    // if the entry for 'newname' exists, make sure it can be replaced by
    // the vnode behind 'oldname'.
    fbl::unique_ptr<WritebackWork> wb;
    if ((status = fs_->CreateWork(&wb)) != ZX_OK) {
        return status;
    }
    args.wb = wb.get();
    args.name = newname;
    args.ino = oldvn->ino_;
    args.type = oldvn->IsDirectory() ? kMinfsTypeDir : kMinfsTypeFile;
    status = newdir->ForEachDirent(&args, DirentCallbackAttemptRename);
    if (status == ZX_ERR_NOT_FOUND) {
        // if 'newname' does not exist, create it
        args.reclen = static_cast<uint32_t>(DirentSize(static_cast<uint8_t>(newname.length())));
        if ((status = newdir->ForEachDirent(&args, DirentCallbackAppend)) < 0) {
            return status;
        }
    } else if (status != ZX_OK) {
        return status;
    }

    // update the oldvn's entry for '..' if (1) it was a directory, and (2) it
    // moved to a new directory
    if ((args.type == kMinfsTypeDir) && (ino_ != newdir->ino_)) {
        fbl::RefPtr<fs::Vnode> vn_fs;
        if ((status = newdir->Lookup(&vn_fs, newname)) < 0) {
            return status;
        }
        auto vn = fbl::RefPtr<VnodeMinfs>::Downcast(vn_fs);
        args.name = "..";
        args.ino = newdir->ino_;
        if ((status = vn->ForEachDirent(&args, DirentCallbackUpdateInode)) < 0) {
            return status;
        }
    }

    // at this point, the oldvn exists with multiple names (or the same name in
    // different directories)
    oldvn->inode_.link_count++;

    // finally, remove oldname from its original position
    args.name = oldname;
    status = ForEachDirent(&args, DirentCallbackForceUnlink);
    wb->PinVnode(oldvn);
    wb->PinVnode(newdir);
    fs_->EnqueueWork(fbl::move(wb));
    success = true;
    return ZX_OK;
}

zx_status_t VnodeMinfs::Link(fbl::StringPiece name, fbl::RefPtr<fs::Vnode> _target) {
    TRACE_DURATION("minfs", "VnodeMinfs::Link", "name", name);
    ZX_DEBUG_ASSERT(fs::vfs_valid_name(name));

    if (!IsDirectory()) {
        return ZX_ERR_NOT_SUPPORTED;
    } else if (IsUnlinked()) {
        return ZX_ERR_BAD_STATE;
    }

    auto target = fbl::RefPtr<VnodeMinfs>::Downcast(_target);
    if (target->IsDirectory()) {
        // The target must not be a directory
        return ZX_ERR_NOT_FILE;
    }

    // The destination should not exist
    DirArgs args = DirArgs();
    args.name = name;
    zx_status_t status;
    if ((status = ForEachDirent(&args, DirentCallbackFind)) != ZX_ERR_NOT_FOUND) {
        return (status == ZX_OK) ? ZX_ERR_ALREADY_EXISTS : status;
    }

    fbl::unique_ptr<WritebackWork> wb;
    if ((status = fs_->CreateWork(&wb)) != ZX_OK) {
        return status;
    }
    args.ino = target->ino_;
    args.type = kMinfsTypeFile; // We can't hard link directories
    args.reclen = static_cast<uint32_t>(DirentSize(static_cast<uint8_t>(name.length())));
    args.wb = wb.get();
    if ((status = ForEachDirent(&args, DirentCallbackAppend)) < 0) {
        return status;
    }

    // We have successfully added the vn to a new location. Increment the link count.
    target->inode_.link_count++;
    target->InodeSync(wb.get(), kMxFsSyncDefault);
    wb->PinVnode(fbl::move(fbl::WrapRefPtr(this)));
    wb->PinVnode(target);
    fs_->EnqueueWork(fbl::move(wb));
    return ZX_OK;
}

#ifdef __Fuchsia__
zx_status_t VnodeMinfs::GetHandles(uint32_t flags, zx_handle_t* hnd, uint32_t* type,
                                   zxrio_object_info_t* extra) {
    if (IsDirectory()) {
        *type = FDIO_PROTOCOL_DIRECTORY;
    } else {
        *type = FDIO_PROTOCOL_FILE;
    }
    return ZX_OK;
}

void VnodeMinfs::Sync(SyncCallback closure) {
    TRACE_DURATION("minfs", "VnodeMinfs::Sync");
    fs_->Sync([this, cb = fbl::move(closure)](zx_status_t status) {
        if (status != ZX_OK) {
            cb(status);
            return;
        }
        status = fs_->bc_->Sync();
        cb(status);
    });
    return;
}

zx_status_t VnodeMinfs::AttachRemote(fs::MountChannel h) {
    if (kMinfsRootIno == ino_) {
        return ZX_ERR_ACCESS_DENIED;
    } else if (!IsDirectory() || IsUnlinked()) {
        return ZX_ERR_NOT_DIR;
    } else if (IsRemote()) {
        return ZX_ERR_ALREADY_BOUND;
    }
    SetRemote(fbl::move(h.TakeChannel()));
    return ZX_OK;
}
#endif

VnodeMinfs::DirectArgs VnodeMinfs::IndirectArgs::GetDirect(blk_t* barray, unsigned ibindex) const {
    // Determine the starting index for direct blocks within this indirect block
    blk_t direct_start = ibindex == 0 ? bindex_ : 0;

    // Determine how many direct blocks have already been op'd in indirect block context
    blk_t found = 0;

    if (ibindex) {
        found = kMinfsDirectPerIndirect * ibindex - bindex_;
    }

    DirectArgs params(op_, // op
                      &barray[direct_start], // array
                      fbl::min(count_ - found, kMinfsDirectPerIndirect - direct_start), // count
                      bnos_ == nullptr ? nullptr : &bnos_[found]); // bnos
    return params;
}

VnodeMinfs::IndirectArgs VnodeMinfs::DindirectArgs::GetIndirect(blk_t* iarray,
                                                                unsigned dibindex) const {
    // Determine relative starting indices for indirect and direct blocks
    uint32_t indirect_start = dibindex == 0 ? ibindex_ : 0;
    uint32_t direct_start = (dibindex == 0 && indirect_start == ibindex_) ? bindex_ : 0;

    // Determine how many direct blocks we have already op'd within doubly indirect
    // context
    blk_t found = 0;
    if (dibindex) {
        found = kMinfsDirectPerIndirect * kMinfsDirectPerIndirect * dibindex -
                (ibindex_ * kMinfsDirectPerIndirect) + bindex_;
    }

    IndirectArgs params(op_, // op
                        &iarray[indirect_start], // array
                        fbl::min(count_ - found, kMinfsDirectPerDindirect - direct_start), // count
                        bnos_ == nullptr ? nullptr : &bnos_[found], // bnos
                        direct_start, // bindex
                        ib_vmo_offset_ + dibindex + ibindex_ // ib_vmo_offset
                        );
    return params;
}

} // namespace minfs
