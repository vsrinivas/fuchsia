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
#include <magenta/device/vfs.h>

#ifdef __Fuchsia__
#include <magenta/syscalls.h>
#include <mxio/vfs.h>
#endif

#include "minfs-private.h"

namespace {

mx_time_t minfs_gettime_utc() {
    // linux/magenta compatible
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    mx_time_t time = MX_SEC(ts.tv_sec)+ts.tv_nsec;
    return time;
}

} // namespace anonymous

namespace minfs {

#ifdef __Fuchsia__
mx_status_t VnodeMinfs::VmoReadExact(void* data, uint64_t offset, size_t len) {
    size_t actual;
    mx_status_t status = vmo_.read(data, offset, len, &actual);
    if (status != MX_OK) {
        return status;
    } else if (actual != len) {
        return MX_ERR_IO;
    }
    return MX_OK;
}

mx_status_t VnodeMinfs::VmoWriteExact(const void* data, uint64_t offset, size_t len) {
    size_t actual;
    mx_status_t status = vmo_.write(data, offset, len, &actual);
    if (status != MX_OK) {
        return status;
    } else if (actual != len) {
        return MX_ERR_IO;
    }
    return MX_OK;
}
#endif


void VnodeMinfs::InodeSync(WriteTxn* txn, uint32_t flags) {
    // by default, c/mtimes are not updated to current time
    if (flags != kMxFsSyncDefault) {
        mx_time_t cur_time = minfs_gettime_utc();
        // update times before syncing
        if ((flags & kMxFsSyncMtime) != 0) {
            inode_.modify_time = cur_time;
        }
        if ((flags & kMxFsSyncCtime) != 0) {
            inode_.create_time = cur_time;
        }
    }

    fs_->InodeSync(txn, ino_, &inode_);
}

mx_status_t VnodeMinfs::BlocksShrinkDirect(WriteTxn *txn, size_t count, blk_t* barray,
                                           bool* dirty) {
    // release direct blocks
    for (unsigned i = 0; i < count; i++) {
        if (barray[i] == 0) {
            continue;
        }
        fs_->ValidateBno(barray[i]);
        fs_->BlockFree(txn, barray[i]);
        barray[i] = 0;
        inode_.block_count--;
        *dirty = true;
    }

    return MX_OK;
}

mx_status_t VnodeMinfs::BlocksShrinkIndirect(WriteTxn* txn, uint32_t bindex, size_t count,
                                             uint32_t ib_vmo_offset, blk_t* iarray,
                                             bool* dirty) {
    // release indirect blocks
    for (unsigned i = 0; i < count; i++) {
        if (iarray[i] == 0) {
            continue;
        }
        fs_->ValidateBno(iarray[i]);

#ifdef __Fuchsia__
        uint32_t* entry;
        ReadIndirectVmoBlock(ib_vmo_offset + i, &entry);
#else
        uint32_t entry[kMinfsBlockSize];
        ReadIndirectBlock(iarray[i], entry);
#endif

        // release the blocks pointed at by the entries in the indirect block
        mx_status_t status;
        uint32_t direct_start = i == 0 ? bindex : 0;
        if ((status = BlocksShrinkDirect(txn, kMinfsDirectPerIndirect - direct_start,
                                         &entry[direct_start], dirty)) != MX_OK) {
            return status;
        }

        // only update the indirect block if an entry was deleted
        if (*dirty) {
#ifdef __Fuchsia__
            txn->Enqueue(vmoid_indirect_, ib_vmo_offset + i, iarray[i] + fs_->info_.dat_block, 1);
#else
            fs_->bc_->Writeblk(iarray[i] + fs_->info_.dat_block, entry);
#endif
        }

        // Only delete the indirect block if all direct blocks have been deleted
        if (direct_start == 0)  {
            // release the direct block itself
            fs_->BlockFree(txn, iarray[i]);
            iarray[i] = 0;
            inode_.block_count--;
            *dirty = true;
        }
    }

    return MX_OK;
}

mx_status_t VnodeMinfs::BlocksShrinkDoublyIndirect(WriteTxn *txn, uint32_t ibindex, uint32_t bindex,
                                                   size_t count, uint32_t dib_vmo_offset,
                                                   uint32_t ib_vmo_offset, blk_t* diarray,
                                                   bool* dirty) {
    // release doubly indirect blocks
    for (unsigned i = 0; i < count; i++) {
        if (diarray[i] == 0) {
            continue;
        }

        fs_->ValidateBno(diarray[i]);

#ifdef __Fuchsia__
        uint32_t* dientry;
        ReadIndirectVmoBlock(GetVmoOffsetForDoublyIndirect(i), &dientry);
#else
        uint32_t dientry[kMinfsBlockSize];
        ReadIndirectBlock(diarray[i], dientry);
#endif

        // release the blocks pointed at by the entries in the indirect block
        uint32_t indirect_start = i == 0 ? ibindex : 0;
        uint32_t direct_start = (i == 0 && indirect_start == ibindex) ? bindex : 0;
        mx_status_t status;
        if ((status = BlocksShrinkIndirect(txn, direct_start,
                                           kMinfsDirectPerIndirect - indirect_start,
                                           ib_vmo_offset + i + indirect_start,
                                           &dientry[indirect_start], dirty))
            != MX_OK) {
            return status;
        }

        // only update the indirect block if an entry was deleted
        if (*dirty) {
#ifdef __Fuchsia__
            txn->Enqueue(vmoid_indirect_, dib_vmo_offset + i, diarray[i] + fs_->info_.dat_block, 1);
#else
            fs_->bc_->Writeblk(diarray[i] + fs_->info_.dat_block, dientry);
#endif
        }

        // Only delete the doubly indirect block if all indirect blocks have been deleted
        if (indirect_start == 0 && direct_start == 0)  {
            // release the doubly indirect block itself
            fs_->BlockFree(txn, diarray[i]);
            diarray[i] = 0;
            inode_.block_count--;
            *dirty = true;
        }
    }

    return MX_OK;
}

// Delete all blocks (relative to a file) from "start" (inclusive) to the end of
// the file. Does not update mtime/atime.
mx_status_t VnodeMinfs::BlocksShrink(WriteTxn *txn, blk_t start) {
    bool dirty = false;
    mx_status_t status = MX_OK;
    size_t size = (kMinfsIndirect + kMinfsDoublyIndirect) * kMinfsBlockSize;

    size_t count = start <= kMinfsDirect ? kMinfsDirect - start : 0;
    if ((status = BlocksShrinkDirect(txn, count, &inode_.dnum[start], &dirty))
        != MX_OK) {
        return status;
    }

    if (start < kMinfsDirect) {
        start = 0;
    } else {
        start -= kMinfsDirect;
    }

    uint32_t ibindex = start / kMinfsDirectPerIndirect;
    uint32_t bindex = start % kMinfsDirectPerIndirect;
    count = ibindex <= kMinfsIndirect ? kMinfsIndirect - ibindex : 0;
    if ((status = BlocksShrinkIndirect(txn, bindex, count, 0, &inode_.inum[ibindex], &dirty))
        != MX_OK) {
        return status;
    }

    if (start < kMinfsIndirect * kMinfsDirectPerIndirect) {
        start = 0;
    } else {
        start -= kMinfsIndirect * kMinfsDirectPerIndirect;

        uint32_t last_dindirect = start / (kMinfsDirectPerIndirect * kMinfsDirectPerIndirect);
        uint32_t first_indirect = start % (kMinfsDirectPerIndirect * kMinfsDirectPerIndirect);

        if (first_indirect > 0) {
            size = GetVmoSizeForIndirect(last_dindirect);
        } else if (last_dindirect > 0) {
            size = GetVmoSizeForIndirect(last_dindirect - 1);
        }
    }

    uint32_t dibindex = start / (kMinfsDirectPerIndirect * kMinfsDirectPerIndirect);
    start %= kMinfsDirectPerIndirect * kMinfsDirectPerIndirect;
    ibindex = start / kMinfsDirectPerIndirect;
    bindex = start % kMinfsDirectPerIndirect;
    count = dibindex <= kMinfsDoublyIndirect ? kMinfsDoublyIndirect - dibindex : 0;
    if ((status = BlocksShrinkDoublyIndirect(txn, ibindex, bindex, count,
                                             GetVmoOffsetForDoublyIndirect(dibindex),
                                             GetVmoOffsetForIndirect(dibindex),
                                             &inode_.dinum[dibindex], &dirty)) != MX_OK) {
        return status;
    }

#ifdef __Fuchsia__
    if (vmo_indirect_ != nullptr && vmo_indirect_->GetSize() > size) {
        if ((status = vmo_indirect_->Shrink(0, size)) != MX_OK) {
            return status;
        }
    }
#endif

    if (dirty) {
        InodeSync(txn, kMxFsSyncDefault);
    }

    return MX_OK;
}

#ifdef __Fuchsia__
mx_status_t VnodeMinfs::LoadIndirectBlocks(blk_t* iarray, uint32_t count, uint32_t offset,
                                           uint64_t size) {
    mx_status_t status;
    if ((status = InitIndirectVmo()) != MX_OK) {
        return status;
    }

    if (vmo_indirect_->GetSize() < size) {
        mx_status_t status;
        if ((status = vmo_indirect_->Grow(size)) != MX_OK) {
            return status;
        }
    }

    ReadTxn txn(fs_->bc_.get());

    for (uint32_t i = 0; i < count; i++) {
        blk_t ibno;
        if ((ibno = iarray[i]) != 0) {
            fs_->ValidateBno(ibno);
            txn.Enqueue(vmoid_indirect_, offset + i, ibno + fs_->info_.dat_block, 1);
        }
    }

    return txn.Flush();
}

mx_status_t VnodeMinfs::LoadIndirectWithinDoublyIndirect(uint32_t dindex) {
    uint32_t* dientry;
    ReadIndirectVmoBlock(GetVmoOffsetForDoublyIndirect(dindex), &dientry);
    return LoadIndirectBlocks(dientry, kMinfsDirectPerIndirect, GetVmoOffsetForIndirect(dindex),
                              GetVmoSizeForIndirect(dindex));
}

mx_status_t VnodeMinfs::InitIndirectVmo() {
    if (vmo_indirect_ != nullptr) {
        return MX_OK;
    }

    mx_status_t status;
    if ((status = MappedVmo::Create(kMinfsBlockSize * (kMinfsIndirect + kMinfsDoublyIndirect),
                                    "minfs-indirect", &vmo_indirect_)) != MX_OK) {
        return status;
    }
    if ((status = fs_->bc_->AttachVmo(vmo_indirect_->GetVmo(), &vmoid_indirect_)) != MX_OK) {
        vmo_indirect_ = nullptr;
        return status;
    }

    // Load initial set of indirect blocks
    if ((status = LoadIndirectBlocks(inode_.inum, kMinfsIndirect, 0, 0)) != MX_OK) {
        vmo_indirect_ = nullptr;
        return status;
    }

    // Load doubly indirect blocks
    if ((status = LoadIndirectBlocks(inode_.dinum, kMinfsDoublyIndirect,
                                     GetVmoOffsetForDoublyIndirect(0),
                                     GetVmoSizeForDoublyIndirect()) != MX_OK)) {
        vmo_indirect_ = nullptr;
        return status;
    }

    return MX_OK;
}

// Since we cannot yet register the filesystem as a paging service (and cleanly
// fault on pages when they are actually needed), we currently read an entire
// file to a VMO when a file's data block are accessed.
//
// TODO(smklein): Even this hack can be optimized; a bitmap could be used to
// track all 'empty/read/dirty' blocks for each vnode, rather than reading
// the entire file.
mx_status_t VnodeMinfs::InitVmo() {
    if (vmo_.is_valid()) {
        return MX_OK;
    }

    mx_status_t status;
    if ((status = mx::vmo::create(fbl::roundup(inode_.size, kMinfsBlockSize),
                                  0, &vmo_)) != MX_OK) {
        FS_TRACE_ERROR("Failed to initialize vmo; error: %d\n", status);
        return status;
    }

    mx_object_set_property(vmo_.get(), MX_PROP_NAME, "minfs-inode", 11);

    if ((status = fs_->bc_->AttachVmo(vmo_.get(), &vmoid_)) != MX_OK) {
        vmo_.reset();
        return status;
    }
    ReadTxn txn(fs_->bc_.get());

    // Initialize all direct blocks
    blk_t bno;
    for (uint32_t d = 0; d < kMinfsDirect; d++) {
        if ((bno = inode_.dnum[d]) != 0) {
            fs_->ValidateBno(bno);
            txn.Enqueue(vmoid_, d, bno + fs_->info_.dat_block, 1);
        }
    }

    // Initialize all indirect blocks
    for (uint32_t i = 0; i < kMinfsIndirect; i++) {
        blk_t ibno;
        if ((ibno = inode_.inum[i]) != 0) {
            fs_->ValidateBno(ibno);

            // Only initialize the indirect vmo if it is being used.
            if ((status = InitIndirectVmo()) != MX_OK) {
                vmo_.reset();
                return status;
            }

            uint32_t* ientry;
            ReadIndirectVmoBlock(i, &ientry);

            for (uint32_t j = 0; j < kMinfsDirectPerIndirect; j++) {
                if ((bno = ientry[j]) != 0) {
                    fs_->ValidateBno(bno);
                    uint32_t n = kMinfsDirect + i * kMinfsDirectPerIndirect + j;
                    txn.Enqueue(vmoid_, n, bno + fs_->info_.dat_block, 1);
                }
            }
        }
    }

    // Initialize all doubly indirect blocks
    for (uint32_t i = 0; i < kMinfsDoublyIndirect; i++) {
        blk_t dibno;

        if ((dibno = inode_.dinum[i]) != 0) {
            fs_->ValidateBno(dibno);

            // Only initialize the doubly indirect vmo if it is being used.
            if ((status = InitIndirectVmo()) != MX_OK) {
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
                    if ((status = LoadIndirectWithinDoublyIndirect(i)) != MX_OK) {
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
                            txn.Enqueue(vmoid_, n, bno, 1);
                        }
                    }
                }
            }
        }
    }

    return txn.Flush();
}
#endif

mx_status_t VnodeMinfs::GetBnoDirect(WriteTxn* txn, blk_t* bno, bool* dirty) {
    // direct blocks are simple... is there an entry in dnum[]?
    blk_t hint = 0;

    if ((*bno == 0) && (txn != nullptr)) {
        // allocate a new block
        mx_status_t status = fs_->BlockNew(txn, hint, bno);
        if (status != MX_OK) {
            return status;
        }
        inode_.block_count++;
        *dirty = true;
    }

    fs_->ValidateBno(*bno);
    return MX_OK;
}

mx_status_t VnodeMinfs::GetBnoIndirect(WriteTxn* txn, uint32_t bindex, uint32_t ib_vmo_offset,
                                       blk_t* ibno, blk_t* bno, bool* dirty) {
    // we should have initialized vmo before calling this method
    mx_status_t status;

#ifdef __Fuchsia__
    if ((status = VnodeMinfs::InitIndirectVmo()) != MX_OK) {
        return status;
    }
#endif

    // retrieve indirect block at this index
    if ((*ibno == 0) && (txn != nullptr)) {
        // allocate new indirect block if it does not exist
        if ((status = fs_->BlockNew(txn, 0, ibno)) != MX_OK) {
            return status;
        }

#ifdef __Fuchsia__
        ClearIndirectVmoBlock(ib_vmo_offset);
#else
        ClearIndirectBlock(*ibno);
#endif

        inode_.block_count++;
        *dirty = true;
    }

#ifdef __Fuchsia__
    uint32_t* ientry;
    ReadIndirectVmoBlock(ib_vmo_offset, &ientry);
#else
    uint32_t ientry[kMinfsBlockSize];
    ReadIndirectBlock(*ibno, ientry);
#endif

    bool direct_dirty = false;
    if ((status = GetBnoDirect(txn, &ientry[bindex], &direct_dirty)) != MX_OK) {
        return status;
    }

    *bno = ientry[bindex];

    if (*dirty || direct_dirty) {
        // Write back the indirect block if a new block was allocated
#ifdef __Fuchsia__
        txn->Enqueue(vmoid_indirect_, ib_vmo_offset, *ibno + fs_->info_.dat_block, 1);
#else
        fs_->bc_->Writeblk(*ibno + fs_->info_.dat_block, ientry);
#endif
        InodeSync(txn, kMxFsSyncDefault);
    }

    return MX_OK;
}

mx_status_t VnodeMinfs::GetBnoDoublyIndirect(WriteTxn* txn, uint32_t ibindex, uint32_t bindex,
                                             uint32_t dib_vmo_offset, uint32_t ib_vmo_offset,
                                             blk_t* dibno, blk_t* bno, bool* dirty) {
    mx_status_t status;
#ifdef __Fuchsia__
    // If the vmo_indirect_ vmo has not been created, make it now.
    if ((status = InitIndirectVmo()) != MX_OK) {
        return status;
    }

    MX_DEBUG_ASSERT(vmo_indirect_ != nullptr);
#endif

    // look up the doubly indirect bno, create if it doesn't already exist
    if ((*dibno == 0) && (txn != nullptr)) {
        // allocate a new doubly indirect block
        if ((status = fs_->BlockNew(txn, 0, dibno)) != MX_OK) {
            return status;
        }

#ifdef __Fuchsia__
        ClearIndirectVmoBlock(dib_vmo_offset);
#else
        ClearIndirectBlock(*dibno);
#endif

        // record new doubly indirect block in inode, note that we need to update
        inode_.block_count++;
        *dirty = true;
    }

    // read from doubly indirect block
#ifdef __Fuchsia__
    uint32_t* dientry;
    ReadIndirectVmoBlock(dib_vmo_offset, &dientry);
#else
    uint32_t dientry[kMinfsBlockSize];
    ReadIndirectBlock(*dibno, dientry);
#endif

    // get indirect block
    bool indirect_dirty = false;
    if ((status = GetBnoIndirect(txn, bindex, ib_vmo_offset + ibindex, &dientry[ibindex], bno,
                                 &indirect_dirty)) != MX_OK) {
        return status;
    }

    if (*dirty || indirect_dirty) {
        // Write back the doubly indirect block if a new block was allocated
#ifdef __Fuchsia__
        txn->Enqueue(vmoid_indirect_, dib_vmo_offset, *dibno +  + fs_->info_.dat_block, 1);
#else
        fs_->bc_->Writeblk(*dibno +  + fs_->info_.dat_block, dientry);
#endif
        InodeSync(txn, kMxFsSyncDefault);
    }

    return MX_OK;
}

#ifdef __Fuchsia__
void VnodeMinfs::ReadIndirectVmoBlock(uint32_t offset, uint32_t** entry) {
    MX_DEBUG_ASSERT(vmo_indirect_ != nullptr);
    uintptr_t addr = reinterpret_cast<uintptr_t>(vmo_indirect_->GetData());
    *entry = reinterpret_cast<uint32_t*>(addr + kMinfsBlockSize * offset);
}

void VnodeMinfs::ClearIndirectVmoBlock(uint32_t offset) {
    MX_DEBUG_ASSERT(vmo_indirect_ != nullptr);
    uintptr_t addr = reinterpret_cast<uintptr_t>(vmo_indirect_->GetData());
    memset(reinterpret_cast<void*>(addr + kMinfsBlockSize * offset), 0, kMinfsBlockSize);
}
#else
void VnodeMinfs::ReadIndirectBlock(blk_t bno, uint32_t* entry) {
    fs_->bc_->Readblk(bno + fs_->info_.dat_block, entry);
}

void VnodeMinfs::ClearIndirectBlock(blk_t bno) {
    uint32_t data[kMinfsBlockSize];
    memset(data, 0, kMinfsBlockSize);
    fs_->bc_->Writeblk(bno + fs_->info_.dat_block, data);
}
#endif

// Get the bno corresponding to the nth logical block within the file.
mx_status_t VnodeMinfs::GetBno(WriteTxn* txn, blk_t n, blk_t* bno) {
    bool dirty = false;

    if (n < kMinfsDirect) {
        mx_status_t status = GetBnoDirect(txn, &inode_.dnum[n], &dirty);
        *bno = inode_.dnum[n];
        return status;
    }

    // for indirect blocks, adjust past the direct blocks
    n -= kMinfsDirect;

    if (n < kMinfsIndirect * kMinfsDirectPerIndirect) {
        // index of indirect block
        uint32_t ibindex = n / kMinfsDirectPerIndirect;
        // index of direct block within indirect block
        uint32_t bindex = n % kMinfsDirectPerIndirect;
        return GetBnoIndirect(txn, bindex, 0, &inode_.inum[ibindex], bno, &dirty);
    }

    // for doubly indirect blocks, adjust past the indirect blocks
    n -= (kMinfsIndirect * kMinfsDirectPerIndirect);

    if (n < kMinfsDoublyIndirect * kMinfsDirectPerIndirect * kMinfsDirectPerIndirect) {
        // index of doubly indirect block
        uint32_t dibindex = n / (kMinfsDirectPerIndirect * kMinfsDirectPerIndirect);
        MX_DEBUG_ASSERT(dibindex < kMinfsDoublyIndirect);
        n -= (dibindex * kMinfsDirectPerIndirect * kMinfsDirectPerIndirect);
        // index of indirect block within doubly indirect block
        uint32_t ibindex = n / kMinfsDirectPerIndirect;
        // index of direct block within indirect block
        uint32_t bindex = n % kMinfsDirectPerIndirect;

    #ifdef __Fuchsia__
        // Grow VMO if we need more space to fit this set of indirect blocks
        mx_status_t status;
        uint64_t vmo_size = GetVmoSizeForIndirect(dibindex);
        if (vmo_indirect_->GetSize() < vmo_size) {
            if ((status = vmo_indirect_->Grow(vmo_size)) != MX_OK) {
                return status;
            }
        }
    #endif

        return GetBnoDoublyIndirect(txn, ibindex, bindex, GetVmoOffsetForDoublyIndirect(dibindex),
                                    GetVmoOffsetForIndirect(dibindex), &inode_.dinum[dibindex], bno,
                                    &dirty);
    }

    return MX_ERR_OUT_OF_RANGE;
}

// Immediately stop iterating over the directory.
#define DIR_CB_DONE 0
// Access the next direntry in the directory. Offsets updated.
#define DIR_CB_NEXT 1
// Identify that the direntry record was modified. Stop iterating.
#define DIR_CB_SAVE_SYNC 2

mx_status_t VnodeMinfs::ReadExactInternal(void* data, size_t len, size_t off) {
    size_t actual;
    mx_status_t status = ReadInternal(data, len, off, &actual);
    if (status != MX_OK) {
        return status;
    } else if (actual != len) {
        return MX_ERR_IO;
    }
    return MX_OK;
}

mx_status_t VnodeMinfs::WriteExactInternal(WriteTxn* txn, const void* data,
                                           size_t len, size_t off) {
    size_t actual;
    mx_status_t status = WriteInternal(txn, data, len, off, &actual);
    if (status != MX_OK) {
        return status;
    } else if (actual != len) {
        return MX_ERR_IO;
    }
    InodeSync(txn, kMxFsSyncMtime);
    return MX_OK;
}

static mx_status_t validate_dirent(minfs_dirent_t* de, size_t bytes_read, size_t off) {
    uint32_t reclen = static_cast<uint32_t>(MinfsReclen(de, off));
    if ((bytes_read < MINFS_DIRENT_SIZE) || (reclen < MINFS_DIRENT_SIZE)) {
        FS_TRACE_ERROR("vn_dir: Could not read dirent at offset: %zd\n", off);
        return MX_ERR_IO;
    } else if ((off + reclen > kMinfsMaxDirectorySize) || (reclen & 3)) {
        FS_TRACE_ERROR("vn_dir: bad reclen %u > %u\n", reclen, kMinfsMaxDirectorySize);
        return MX_ERR_IO;
    } else if (de->ino != 0) {
        if ((de->namelen == 0) ||
            (de->namelen > (reclen - MINFS_DIRENT_SIZE))) {
            FS_TRACE_ERROR("vn_dir: bad namelen %u / %u\n", de->namelen, reclen);
            return MX_ERR_IO;
        }
    }
    return MX_OK;
}

// Updates offset information to move to the next direntry in the directory.
static mx_status_t do_next_dirent(minfs_dirent_t* de, DirectoryOffset* offs) {
    offs->off_prev = offs->off;
    offs->off += MinfsReclen(de, offs->off);
    return DIR_CB_NEXT;
}

static mx_status_t cb_dir_find(fbl::RefPtr<VnodeMinfs> vndir, minfs_dirent_t* de,
                               DirArgs* args, DirectoryOffset* offs) {
    if ((de->ino != 0) && (de->namelen == args->len) &&
        (!memcmp(de->name, args->name, args->len))) {
        args->ino = de->ino;
        args->type = de->type;
        return DIR_CB_DONE;
    } else {
        return do_next_dirent(de, offs);
    }
}

mx_status_t VnodeMinfs::CanUnlink() const {
    // directories must be empty (dirent_count == 2)
    if (IsDirectory()) {
        if (inode_.dirent_count != 2) {
            // if we have more than "." and "..", not empty, cannot unlink
            return MX_ERR_NOT_EMPTY;
#ifdef __Fuchsia__
        } else if (IsRemote()) {
            // we cannot unlink mount points
            return MX_ERR_UNAVAILABLE;
#endif
        }
    }
    return MX_OK;
}

mx_status_t VnodeMinfs::UnlinkChild(WriteTxn* txn,
                                    fbl::RefPtr<VnodeMinfs> childvn,
                                    minfs_dirent_t* de, DirectoryOffset* offs) {
    // Coalesce the current dirent with the previous/next dirent, if they
    // (1) exist and (2) are free.
    size_t off_prev = offs->off_prev;
    size_t off = offs->off;
    size_t off_next = off + MinfsReclen(de, off);
    minfs_dirent_t de_prev, de_next;
    mx_status_t status;

    // Read the direntries we're considering merging with.
    // Verify they are free and small enough to merge.
    size_t coalesced_size = MinfsReclen(de, off);
    // Coalesce with "next" first, so the kMinfsReclenLast bit can easily flow
    // back to "de" and "de_prev".
    if (!(de->reclen & kMinfsReclenLast)) {
        size_t len = MINFS_DIRENT_SIZE;
        if ((status = ReadExactInternal(&de_next, len, off_next)) != MX_OK) {
            FS_TRACE_ERROR("unlink: Failed to read next dirent\n");
            return status;
        } else if ((status = validate_dirent(&de_next, len, off_next)) != MX_OK) {
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
        if ((status = ReadExactInternal(&de_prev, len, off_prev)) != MX_OK) {
            FS_TRACE_ERROR("unlink: Failed to read previous dirent\n");
            return status;
        } else if ((status = validate_dirent(&de_prev, len, off_prev)) != MX_OK) {
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
        return MX_ERR_IO;
    }
    de->ino = 0;
    de->reclen = static_cast<uint32_t>(coalesced_size & kMinfsReclenMask) |
        (de->reclen & kMinfsReclenLast);
    // Erase dirent (replace with 'empty' dirent)
    if ((status = WriteExactInternal(txn, de, MINFS_DIRENT_SIZE, off)) != MX_OK) {
        return status;
    }

    if (de->reclen & kMinfsReclenLast) {
        // Truncating the directory merely removed unused space; if it fails,
        // the directory contents are still valid.
        TruncateInternal(txn, off + MINFS_DIRENT_SIZE);
    }

    inode_.dirent_count--;

    if (MinfsMagicType(childvn->inode_.magic) == kMinfsTypeDir) {
        // Child directory had '..' which pointed to parent directory
        inode_.link_count--;
    }
    childvn->RemoveInodeLink(txn);
    return DIR_CB_SAVE_SYNC;
}

void VnodeMinfs::RemoveInodeLink(WriteTxn* txn) {
    // This effectively 'unlinks' the target node without deleting the direntry
    inode_.link_count--;
    if (MinfsMagicType(inode_.magic) == kMinfsTypeDir) {
        if (inode_.link_count == 1) {
            // Directories are initialized with two links, since they point
            // to themselves via ".". Thus, when they reach "one link", they
            // are only pointed to by themselves, and should be deleted.
            inode_.link_count--;
            flags_ |= kMinfsFlagDeletedDirectory;
        }
    }

    InodeSync(txn, kMxFsSyncMtime);
}

// caller is expected to prevent unlink of "." or ".."
static mx_status_t cb_dir_unlink(fbl::RefPtr<VnodeMinfs> vndir, minfs_dirent_t* de,
                                 DirArgs* args, DirectoryOffset* offs) {
    if ((de->ino == 0) || (args->len != de->namelen) ||
        memcmp(args->name, de->name, args->len)) {
        return do_next_dirent(de, offs);
    }

    fbl::RefPtr<VnodeMinfs> vn;
    mx_status_t status;
    if ((status = vndir->fs_->VnodeGet(&vn, de->ino)) < 0) {
        return status;
    }

    // If a directory was requested, then only try unlinking a directory
    if ((args->type == kMinfsTypeDir) && !vn->IsDirectory()) {
        return MX_ERR_NOT_DIR;
    }
    if ((status = vn->CanUnlink()) != MX_OK) {
        return status;
    }
    return vndir->UnlinkChild(args->txn, fbl::move(vn), de, offs);
}

// same as unlink, but do not validate vnode
static mx_status_t cb_dir_force_unlink(fbl::RefPtr<VnodeMinfs> vndir, minfs_dirent_t* de,
                                       DirArgs* args, DirectoryOffset* offs) {
    if ((de->ino == 0) || (args->len != de->namelen) ||
        memcmp(args->name, de->name, args->len)) {
        return do_next_dirent(de, offs);
    }

    fbl::RefPtr<VnodeMinfs> vn;
    mx_status_t status;
    if ((status = vndir->fs_->VnodeGet(&vn, de->ino)) < 0) {
        return status;
    }
    return vndir->UnlinkChild(args->txn, fbl::move(vn), de, offs);
}

// Given a (name, inode, type) combination:
//   - If no corresponding 'name' is found, MX_ERR_NOT_FOUND is returned
//   - If the 'name' corresponds to a vnode, check that the target vnode:
//      - Does not have the same inode as the argument inode
//      - Is the same type as the argument 'type'
//      - Is unlinkable
//   - If the previous checks pass, then:
//      - Remove the old vnode (decrement link count by one)
//      - Replace the old vnode's position in the directory with the new inode
static mx_status_t cb_dir_attempt_rename(fbl::RefPtr<VnodeMinfs> vndir, minfs_dirent_t* de,
                                         DirArgs* args, DirectoryOffset* offs) {
    if ((de->ino == 0) || (args->len != de->namelen) ||
        memcmp(args->name, de->name, args->len)) {
        return do_next_dirent(de, offs);
    }

    fbl::RefPtr<VnodeMinfs> vn;
    mx_status_t status;
    if ((status = vndir->fs_->VnodeGet(&vn, de->ino)) < 0) {
        return status;
    } else if (args->ino == vn->ino_) {
        // cannot rename node to itself
        return MX_ERR_BAD_STATE;
    } else if (args->type != de->type) {
        // cannot rename directory to file (or vice versa)
        return MX_ERR_BAD_STATE;
    } else if ((status = vn->CanUnlink()) != MX_OK) {
        // if we cannot unlink the target, we cannot rename the target
        return status;
    }

    // If we are renaming ON TOP of a directory, then we can skip
    // updating the parent link count -- the old directory had a ".." entry to
    // the parent (link count of 1), but the new directory will ALSO have a ".."
    // entry, making the rename operation idempotent w.r.t. the parent link
    // count.
    vn->RemoveInodeLink(args->txn);

    de->ino = args->ino;
    status = vndir->WriteExactInternal(args->txn, de, DirentSize(de->namelen), offs->off);
    if (status != MX_OK) {
        return status;
    }
    return DIR_CB_SAVE_SYNC;
}

static mx_status_t cb_dir_update_inode(fbl::RefPtr<VnodeMinfs> vndir, minfs_dirent_t* de,
                                       DirArgs* args, DirectoryOffset* offs) {
    if ((de->ino == 0) || (args->len != de->namelen) ||
        memcmp(args->name, de->name, args->len)) {
        return do_next_dirent(de, offs);
    }

    de->ino = args->ino;
    mx_status_t status = vndir->WriteExactInternal(args->txn, de,
                                                   DirentSize(de->namelen),
                                                   offs->off);
    if (status != MX_OK) {
        return status;
    }
    return DIR_CB_SAVE_SYNC;
}

static mx_status_t add_dirent(fbl::RefPtr<VnodeMinfs> vndir,
                              minfs_dirent_t* de, DirArgs* args, size_t off) {
    de->ino = args->ino;
    de->type = static_cast<uint8_t>(args->type);
    de->namelen = static_cast<uint8_t>(args->len);
    memcpy(de->name, args->name, args->len);
    mx_status_t status = vndir->WriteExactInternal(args->txn, de,
                                                   DirentSize(de->namelen),
                                                   off);
    if (status != MX_OK) {
        return status;
    }
    vndir->inode_.dirent_count++;
    if (args->type == kMinfsTypeDir) {
        // Child directory has '..' which will point to parent directory
        vndir->inode_.link_count++;
    }
    return DIR_CB_SAVE_SYNC;
}

static mx_status_t cb_dir_append(fbl::RefPtr<VnodeMinfs> vndir, minfs_dirent_t* de,
                                 DirArgs* args, DirectoryOffset* offs) {
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
            return MX_ERR_IO;
        }
        uint32_t extra = reclen - size;
        if (extra < args->reclen) {
            return do_next_dirent(de, offs);
        }
        // shrink existing entry
        bool was_last_record = de->reclen & kMinfsReclenLast;
        de->reclen = size;
        mx_status_t status = vndir->WriteExactInternal(args->txn, de,
                                                       DirentSize(de->namelen),
                                                       offs->off);
        if (status != MX_OK) {
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
mx_status_t VnodeMinfs::ForEachDirent(DirArgs* args, const DirentCallback func) {
    char data[kMinfsMaxDirentSize];
    minfs_dirent_t* de = (minfs_dirent_t*) data;
    DirectoryOffset offs = {
        .off = 0,
        .off_prev = 0,
    };
    while (offs.off + MINFS_DIRENT_SIZE < kMinfsMaxDirectorySize) {
        FS_TRACE(MINFS, "Reading dirent at offset %zd\n", offs.off);
        size_t r;
        mx_status_t status = ReadInternal(data, kMinfsMaxDirentSize, offs.off, &r);
        if (status != MX_OK) {
            return status;
        } else if ((status = validate_dirent(de, r, offs.off)) != MX_OK) {
            return status;
        }

        switch ((status = func(fbl::RefPtr<VnodeMinfs>(this), de, args, &offs))) {
        case DIR_CB_NEXT:
            break;
        case DIR_CB_SAVE_SYNC:
            inode_.seq_num++;
            InodeSync(args->txn, kMxFsSyncMtime);
            return MX_OK;
        case DIR_CB_DONE:
        default:
            return status;
        }
    }
    return MX_ERR_NOT_FOUND;
}

VnodeMinfs::~VnodeMinfs() {
    if (inode_.link_count == 0) {
        fs_->InoFree(this);
    }

    fs_->VnodeRelease(this);
#ifdef __Fuchsia__
    // Detach the vmoids from the underlying block device,
    // so the underlying VMO may be released.
    size_t request_count = 0;
    block_fifo_request_t request[2];
    if (vmo_.is_valid()) {
        request[request_count].txnid = fs_->bc_->TxnId();
        request[request_count].vmoid = vmoid_;
        request[request_count].opcode = BLOCKIO_CLOSE_VMO;
        request_count++;
    }
    if (vmo_indirect_ != nullptr) {
        request[request_count].txnid = fs_->bc_->TxnId();
        request[request_count].vmoid = vmoid_indirect_;
        request[request_count].opcode = BLOCKIO_CLOSE_VMO;
        request_count++;
    }
    if (request_count) {
        fs_->bc_->Txn(&request[0], request_count);
    }
#endif
}

mx_status_t VnodeMinfs::Open(uint32_t flags) {
    FS_TRACE(MINFS, "minfs_open() vn=%p(#%u)\n", this, ino_);
    if ((flags & O_DIRECTORY) && !IsDirectory()) {
        return MX_ERR_NOT_DIR;
    }

    switch (flags & O_ACCMODE) {
    case O_WRONLY:
    case O_RDWR:
        if (IsDirectory()) {
            return MX_ERR_NOT_FILE;
        }
    }

    return MX_OK;
}

ssize_t VnodeMinfs::Read(void* data, size_t len, size_t off) {
    FS_TRACE(MINFS, "minfs_read() vn=%p(#%u) len=%zd off=%zd\n", this, ino_, len, off);
    if (IsDirectory()) {
        return MX_ERR_NOT_FILE;
    }
    size_t r;
    mx_status_t status = ReadInternal(data, len, off, &r);
    if (status != MX_OK) {
        return status;
    }
    return r;
}

// Internal read. Usable on directories.
mx_status_t VnodeMinfs::ReadInternal(void* data, size_t len, size_t off, size_t* actual) {
    // clip to EOF
    if (off >= inode_.size) {
        *actual = 0;
        return MX_OK;
    }
    if (len > (inode_.size - off)) {
        len = inode_.size - off;
    }

    mx_status_t status;
#ifdef __Fuchsia__
    if ((status = InitVmo()) != MX_OK) {
        return status;
    } else if ((status = vmo_.read(data, off, len, actual)) != MX_OK) {
        return status;
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
        if ((status = GetBno(nullptr, n, &bno)) != MX_OK) {
            return status;
        }
        if (bno != 0) {
            char bdata[kMinfsBlockSize];
            if (fs_->bc_->Readblk(bno + fs_->info_.dat_block, bdata)) {
                return MX_ERR_IO;
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
    return MX_OK;
}

ssize_t VnodeMinfs::Write(const void* data, size_t len, size_t off) {
    FS_TRACE(MINFS, "minfs_write() vn=%p(#%u) len=%zd off=%zd\n", this, ino_, len, off);
    if (IsDirectory()) {
        return MX_ERR_NOT_FILE;
    }
    WriteTxn txn(fs_->bc_.get());
    size_t actual;
    mx_status_t status = WriteInternal(&txn, data, len, off, &actual);
    if (status != MX_OK) {
        return status;
    }
    if (actual != 0) {
        InodeSync(&txn, kMxFsSyncMtime);  // Successful writes updates mtime
    }
    return actual;
}

// Internal write. Usable on directories.
mx_status_t VnodeMinfs::WriteInternal(WriteTxn* txn, const void* data,
                                      size_t len, size_t off, size_t* actual) {
    if (len == 0) {
        *actual = 0;
        return MX_OK;
    }

    mx_status_t status;
#ifdef __Fuchsia__
    if ((status = InitVmo()) != MX_OK) {
        return status;
    }
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
            size_t new_size = xfer_off + xfer;
            if ((status = vmo_.set_size(fbl::roundup(new_size, kMinfsBlockSize))) != MX_OK) {
                goto done;
            }
            inode_.size = static_cast<uint32_t>(new_size);
        }

        // Update this block of the in-memory VMO
        if ((status = VmoWriteExact(data, xfer_off, xfer)) != MX_OK) {
            return MX_ERR_IO;
        }

        // Update this block on-disk
        blk_t bno;
        if ((status = GetBno(txn, n, &bno)) != MX_OK) {
            return status;
        }
        MX_DEBUG_ASSERT(bno != 0);
        txn->Enqueue(vmoid_, n, bno + fs_->info_.dat_block, 1);
#else
        blk_t bno;
        if ((status = GetBno(txn, n, &bno)) != MX_OK) {
            goto done;
        }
        MX_DEBUG_ASSERT(bno != 0);
        char wdata[kMinfsBlockSize];
        if (fs_->bc_->Readblk(bno + fs_->info_.dat_block, wdata)) {
            return MX_ERR_IO;
        }
        memcpy(wdata + adjust, data, xfer);
        if (fs_->bc_->Writeblk(bno + fs_->info_.dat_block, wdata)) {
            return MX_ERR_IO;
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
            return MX_ERR_FILE_BIG;
        }

        return MX_ERR_NO_RESOURCES;
    }
    if ((off + len) > inode_.size) {
        inode_.size = static_cast<uint32_t>(off + len);
    }

    *actual = len;
    return MX_OK;
}

mx_status_t VnodeMinfs::Lookup(fbl::RefPtr<fs::Vnode>* out, const char* name, size_t len) {
    FS_TRACE(MINFS, "minfs_lookup() vn=%p(#%u) name='%.*s'\n", this, ino_, (int)len, name);
    MX_DEBUG_ASSERT(fs::vfs_valid_name(name, len));

    if (!IsDirectory()) {
        FS_TRACE_ERROR("not directory\n");
        return MX_ERR_NOT_SUPPORTED;
    }

    return LookupInternal(out, name, len);
}

mx_status_t VnodeMinfs::LookupInternal(fbl::RefPtr<fs::Vnode>* out,
                                       const char* name, size_t len) {
    DirArgs args = DirArgs();
    args.name = name;
    args.len = len;
    mx_status_t status;
    if ((status = ForEachDirent(&args, cb_dir_find)) < 0) {
        return status;
    }
    fbl::RefPtr<VnodeMinfs> vn;
    if ((status = fs_->VnodeGet(&vn, args.ino)) < 0) {
        return status;
    }
    *out = fbl::move(vn);
    return MX_OK;
}

mx_status_t VnodeMinfs::Getattr(vnattr_t* a) {
    FS_TRACE(MINFS, "minfs_getattr() vn=%p(#%u)\n", this, ino_);
    a->mode = DTYPE_TO_VTYPE(MinfsMagicType(inode_.magic)) |
            V_IRUSR | V_IWUSR | V_IRGRP | V_IROTH;
    a->inode = ino_;
    a->size = inode_.size;
    a->blksize = kMinfsBlockSize;
    a->blkcount = inode_.block_count * (kMinfsBlockSize / VNATTR_BLKSIZE);
    a->nlink = inode_.link_count;
    a->create_time = inode_.create_time;
    a->modify_time = inode_.modify_time;
    return MX_OK;
}

mx_status_t VnodeMinfs::Setattr(vnattr_t* a) {
    int dirty = 0;
    FS_TRACE(MINFS, "minfs_setattr() vn=%p(#%u)\n", this, ino_);
    if ((a->valid & ~(ATTR_CTIME|ATTR_MTIME)) != 0) {
        return MX_ERR_NOT_SUPPORTED;
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
        WriteTxn txn(fs_->bc_.get());
        InodeSync(&txn, kMxFsSyncDefault);
    }
    return MX_OK;
}

typedef struct dircookie {
    size_t off;        // Offset into directory
    uint32_t reserved; // Unused
    uint32_t seqno;    // inode seq no
} dircookie_t;

static_assert(sizeof(dircookie_t) <= sizeof(vdircookie_t),
              "MinFS dircookie too large to fit in IO state");

mx_status_t VnodeMinfs::Readdir(void* cookie, void* dirents, size_t len) {
    FS_TRACE(MINFS, "minfs_readdir() vn=%p(#%u) cookie=%p len=%zd\n", this, ino_, cookie, len);
    dircookie_t* dc = reinterpret_cast<dircookie_t*>(cookie);
    fs::DirentFiller df(dirents, len);

    if (!IsDirectory()) {
        return MX_ERR_NOT_SUPPORTED;
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
                goto fail;
            }
            mx_status_t status = ReadInternal(de, kMinfsMaxDirentSize, off_recovered, &r);
            if ((status != MX_OK) || (validate_dirent(de, r, off_recovered) != MX_OK)) {
                goto fail;
            }
            off_recovered += MinfsReclen(de, off_recovered);
        }
        off = off_recovered;
    }

    while (off + MINFS_DIRENT_SIZE < kMinfsMaxDirectorySize) {
        mx_status_t status = ReadInternal(de, kMinfsMaxDirentSize, off, &r);
        if (status != MX_OK) {
            goto fail;
        } else if (validate_dirent(de, r, off) != MX_OK) {
            goto fail;
        }

        if (de->ino && (de->namelen != 2 || strncmp("..", de->name, de->namelen))) {
            mx_status_t status;
            if ((status = df.Next(de->name, de->namelen, de->type)) != MX_OK) {
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
    r = df.BytesFilled();
    MX_DEBUG_ASSERT(r <= len); // Otherwise, we're overflowing the input buffer.
    return static_cast<mx_status_t>(r);

fail:
    dc->off = 0;
    return MX_ERR_IO;
}

#ifdef __Fuchsia__
VnodeMinfs::VnodeMinfs(Minfs* fs) :
    fs_(fs), vmo_(MX_HANDLE_INVALID), vmo_indirect_(nullptr) {}

void VnodeMinfs::Notify(const char* name, size_t len, unsigned event) { watcher_.Notify(name, len, event); }
mx_status_t VnodeMinfs::WatchDir(mx::channel* out) { return watcher_.WatchDir(out); }
mx_status_t VnodeMinfs::WatchDirV2(fs::Vfs* vfs, const vfs_watch_dir_t* cmd) {
    return watcher_.WatchDirV2(vfs, this, cmd);
}

bool VnodeMinfs::IsRemote() const { return remoter_.IsRemote(); }
mx::channel VnodeMinfs::DetachRemote() { return remoter_.DetachRemote(flags_); }
mx_handle_t VnodeMinfs::WaitForRemote() { return remoter_.WaitForRemote(flags_); }
mx_handle_t VnodeMinfs::GetRemote() const { return remoter_.GetRemote(); }
void VnodeMinfs::SetRemote(mx::channel remote) { return remoter_.SetRemote(fbl::move(remote)); }

#else
VnodeMinfs::VnodeMinfs(Minfs* fs) : fs_(fs) {}
#endif

mx_status_t VnodeMinfs::Allocate(Minfs* fs, uint32_t type, fbl::RefPtr<VnodeMinfs>* out) {
    mx_status_t status = AllocateHollow(fs, out);
    if (status != MX_OK) {
        return status;
    }
    memset(&(*out)->inode_, 0, sizeof((*out)->inode_));
    (*out)->inode_.magic = MinfsMagic(type);
    (*out)->inode_.create_time = (*out)->inode_.modify_time = minfs_gettime_utc();
    (*out)->inode_.link_count = (type == kMinfsTypeDir ? 2 : 1);
    return MX_OK;
}

mx_status_t VnodeMinfs::AllocateHollow(Minfs* fs, fbl::RefPtr<VnodeMinfs>* out) {
    fbl::AllocChecker ac;
    *out = fbl::AdoptRef(new (&ac) VnodeMinfs(fs));
    if (!ac.check()) {
        return MX_ERR_NO_MEMORY;
    }
    return MX_OK;
}

mx_status_t VnodeMinfs::Create(fbl::RefPtr<fs::Vnode>* out, const char* name, size_t len, uint32_t mode) {
    FS_TRACE(MINFS, "minfs_create() vn=%p(#%u) name='%.*s' mode=%#x\n",
          this, ino_, (int)len, name, mode);
    MX_DEBUG_ASSERT(fs::vfs_valid_name(name, len));

    if (!IsDirectory()) {
        return MX_ERR_NOT_SUPPORTED;
    }
    if (IsDeletedDirectory()) {
        return MX_ERR_BAD_STATE;
    }

    DirArgs args = DirArgs();
    args.name = name;
    args.len = len;
    // ensure file does not exist
    mx_status_t status;
    if ((status = ForEachDirent(&args, cb_dir_find)) != MX_ERR_NOT_FOUND) {
        return MX_ERR_ALREADY_EXISTS;
    }

    // creating a directory?
    uint32_t type = S_ISDIR(mode) ? kMinfsTypeDir : kMinfsTypeFile;

    WriteTxn txn(fs_->bc_.get());
    // mint a new inode and vnode for it
    fbl::RefPtr<VnodeMinfs> vn;
    if ((status = fs_->VnodeNew(&txn, &vn, type)) < 0) {
        return status;
    }

    // If the new node is a directory, fill it with '.' and '..'.
    if (type == kMinfsTypeDir) {
        char bdata[DirentSize(1) + DirentSize(2)];
        minfs_dir_init(bdata, vn->ino_, ino_);
        size_t expected = DirentSize(1) + DirentSize(2);
        if (vn->WriteExactInternal(&txn, bdata, expected, 0) != MX_OK) {
            return MX_ERR_IO;
        }
        vn->inode_.dirent_count = 2;
        vn->InodeSync(&txn, kMxFsSyncDefault);
    }

    // add directory entry for the new child node
    args.ino = vn->ino_;
    args.type = type;
    args.reclen = static_cast<uint32_t>(DirentSize(static_cast<uint8_t>(len)));
    args.txn = &txn;
    if ((status = ForEachDirent(&args, cb_dir_append)) < 0) {
        return status;
    }

    *out = fbl::move(vn);
    return MX_OK;
}

constexpr const char kFsName[] = "minfs";

ssize_t VnodeMinfs::Ioctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                          size_t out_len) {
    switch (op) {
        case IOCTL_VFS_QUERY_FS: {
            if (out_len < (sizeof(vfs_query_info_t) + strlen(kFsName))) {
                return MX_ERR_INVALID_ARGS;
            }

            vfs_query_info_t* info = static_cast<vfs_query_info_t*>(out_buf);
            info->total_bytes = fs_->info_.block_count * fs_->info_.block_size;
            info->used_bytes = fs_->info_.alloc_block_count * fs_->info_.block_size;
            info->total_nodes = fs_->info_.inode_count;
            info->used_nodes = fs_->info_.alloc_inode_count;
            memcpy(info->name, kFsName, strlen(kFsName));
            return sizeof(vfs_query_info_t) + strlen(kFsName);
        }
        case IOCTL_VFS_UNMOUNT_FS: {
            mx_status_t status = Sync();
            if (status != MX_OK) {
                FS_TRACE_ERROR("minfs unmount failed to sync; unmounting anyway: %d\n", status);
            }
            // 'fs_' is deleted after Unmount is called.
            return fs_->Unmount();
        }
#ifdef __Fuchsia__
        case IOCTL_VFS_GET_DEVICE_PATH: {
            ssize_t len = fs_->bc_->GetDevicePath(static_cast<char*>(out_buf), out_len);

            if ((ssize_t)out_len < len) {
                return MX_ERR_INVALID_ARGS;
            }

            return len;
        }
#endif
        default: {
            return MX_ERR_NOT_SUPPORTED;
        }
    }
}

mx_status_t VnodeMinfs::Unlink(const char* name, size_t len, bool must_be_dir) {
    FS_TRACE(MINFS, "minfs_unlink() vn=%p(#%u) name='%.*s'\n", this, ino_, (int)len, name);
    MX_DEBUG_ASSERT(fs::vfs_valid_name(name, len));

    if (!IsDirectory()) {
        return MX_ERR_NOT_SUPPORTED;
    }
    WriteTxn txn(fs_->bc_.get());
    DirArgs args = DirArgs();
    args.name = name;
    args.len = len;
    args.type = must_be_dir ? kMinfsTypeDir : 0;
    args.txn = &txn;
    return ForEachDirent(&args, cb_dir_unlink);
}

mx_status_t VnodeMinfs::Truncate(size_t len) {
    if (IsDirectory()) {
        return MX_ERR_NOT_FILE;
    }

    WriteTxn txn(fs_->bc_.get());
    mx_status_t status = TruncateInternal(&txn, len);
    if (status == MX_OK) {
        // Successful truncates update inode
        InodeSync(&txn, kMxFsSyncMtime);
    }
    return status;
}

mx_status_t VnodeMinfs::TruncateInternal(WriteTxn* txn, size_t len) {
    mx_status_t r = 0;
#ifdef __Fuchsia__
    // TODO(smklein): We should only init up to 'len'; no need
    // to read in the portion of a large file we plan on deleting.
    if (InitVmo() != MX_OK) {
        return MX_ERR_IO;
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
            if ((r = BlocksShrink(txn, start_bno)) < 0) {
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
            if (GetBno(nullptr, rel_bno, &bno) != MX_OK) {
                return MX_ERR_IO;
            }
            if (bno != 0) {
                size_t adjust = len % kMinfsBlockSize;
#ifdef __Fuchsia__
                if ((r = VmoReadExact(bdata, len - adjust, adjust)) != MX_OK) {
                    return MX_ERR_IO;
                }
                memset(bdata + adjust, 0, kMinfsBlockSize - adjust);

                if ((r = VmoWriteExact(bdata, len - adjust, kMinfsBlockSize)) != MX_OK) {
                    return MX_ERR_IO;
                }
                txn->Enqueue(vmoid_, rel_bno, bno + fs_->info_.dat_block, 1);
#else
                if (fs_->bc_->Readblk(bno + fs_->info_.dat_block, bdata)) {
                    return MX_ERR_IO;
                }
                memset(bdata + adjust, 0, kMinfsBlockSize - adjust);
                if (fs_->bc_->Writeblk(bno + fs_->info_.dat_block, bdata)) {
                    return MX_ERR_IO;
                }
#endif
            }
        }
    } else if (len > inode_.size) {
        // Truncate should make the file longer, filled with zeroes.
        if (kMinfsMaxFileSize < len) {
            return MX_ERR_INVALID_ARGS;
        }
        char zero = 0;
        if ((r = WriteExactInternal(txn, &zero, 1, len - 1)) != MX_OK) {
            return r;
        }
    }

    inode_.size = static_cast<uint32_t>(len);
#ifdef __Fuchsia__
    if ((r = vmo_.set_size(fbl::roundup(len, kMinfsBlockSize))) != MX_OK) {
        return r;
    }
#endif

    return MX_OK;
}

// verify that the 'newdir' inode is not a subdirectory of the source.
static mx_status_t check_not_subdirectory(fbl::RefPtr<VnodeMinfs> src, fbl::RefPtr<VnodeMinfs> newdir) {
    fbl::RefPtr<VnodeMinfs> vn = newdir;
    mx_status_t status = MX_OK;
    while (vn->ino_ != kMinfsRootIno) {
        if (vn->ino_ == src->ino_) {
            status = MX_ERR_INVALID_ARGS;
            break;
        }

        fbl::RefPtr<fs::Vnode> out = nullptr;
        if ((status = vn->LookupInternal(&out, "..", 2)) < 0) {
            break;
        }
        vn = fbl::RefPtr<VnodeMinfs>::Downcast(out);
    }
    return status;
}

mx_status_t VnodeMinfs::Rename(fbl::RefPtr<fs::Vnode> _newdir, const char* oldname, size_t oldlen,
                               const char* newname, size_t newlen, bool src_must_be_dir,
                               bool dst_must_be_dir) {
    auto newdir = fbl::RefPtr<VnodeMinfs>::Downcast(_newdir);
    FS_TRACE(MINFS, "minfs_rename() olddir=%p(#%u) newdir=%p(#%u) oldname='%.*s' newname='%.*s'\n",
          this, ino_, newdir.get(), newdir->ino_, (int)oldlen, oldname, (int)newlen, newname);
    MX_DEBUG_ASSERT(fs::vfs_valid_name(oldname, oldlen));
    MX_DEBUG_ASSERT(fs::vfs_valid_name(newname, newlen));

    // ensure that the vnodes containing oldname and newname are directories
    if (!(IsDirectory() && newdir->IsDirectory()))
        return MX_ERR_NOT_SUPPORTED;

    mx_status_t status;
    fbl::RefPtr<VnodeMinfs> oldvn = nullptr;
    // acquire the 'oldname' node (it must exist)
    DirArgs args = DirArgs();
    args.name = oldname;
    args.len = oldlen;
    if ((status = ForEachDirent(&args, cb_dir_find)) < 0) {
        return status;
    } else if ((status = fs_->VnodeGet(&oldvn, args.ino)) < 0) {
        return status;
    } else if ((status = check_not_subdirectory(oldvn, newdir)) < 0) {
        return status;
    }

    // If either the 'src' or 'dst' must be directories, BOTH of them must be directories.
    if (!oldvn->IsDirectory() && (src_must_be_dir || dst_must_be_dir)) {
        return MX_ERR_NOT_DIR;
    }

    // if the entry for 'newname' exists, make sure it can be replaced by
    // the vnode behind 'oldname'.
    WriteTxn txn(fs_->bc_.get());
    args.txn = &txn;
    args.name = newname;
    args.len = newlen;
    args.ino = oldvn->ino_;
    args.type = oldvn->IsDirectory() ? kMinfsTypeDir : kMinfsTypeFile;
    status = newdir->ForEachDirent(&args, cb_dir_attempt_rename);
    if (status == MX_ERR_NOT_FOUND) {
        // if 'newname' does not exist, create it
        args.reclen = static_cast<uint32_t>(DirentSize(static_cast<uint8_t>(newlen)));
        if ((status = newdir->ForEachDirent(&args, cb_dir_append)) < 0) {
            return status;
        }
    } else if (status != MX_OK) {
        return status;
    }

    // update the oldvn's entry for '..' if (1) it was a directory, and (2) it
    // moved to a new directory
    if ((args.type == kMinfsTypeDir) && (ino_ != newdir->ino_)) {
        fbl::RefPtr<fs::Vnode> vn_fs;
        if ((status = newdir->Lookup(&vn_fs, newname, newlen)) < 0) {
            return status;
        }
        auto vn = fbl::RefPtr<VnodeMinfs>::Downcast(vn_fs);
        args.name = "..";
        args.len = 2;
        args.ino = newdir->ino_;
        if ((status = vn->ForEachDirent(&args, cb_dir_update_inode)) < 0) {
            return status;
        }
    }

    // at this point, the oldvn exists with multiple names (or the same name in
    // different directories)
    oldvn->inode_.link_count++;

    // finally, remove oldname from its original position
    args.name = oldname;
    args.len = oldlen;
    return ForEachDirent(&args, cb_dir_force_unlink);
}

mx_status_t VnodeMinfs::Link(const char* name, size_t len, fbl::RefPtr<fs::Vnode> _target) {
    FS_TRACE(MINFS, "minfs_link() vndir=%p(#%u) name='%.*s'\n", this, ino_, (int)len, name);
    MX_DEBUG_ASSERT(fs::vfs_valid_name(name, len));

    if (!IsDirectory()) {
        return MX_ERR_NOT_SUPPORTED;
    } else if (IsDeletedDirectory()) {
        return MX_ERR_BAD_STATE;
    }

    auto target = fbl::RefPtr<VnodeMinfs>::Downcast(_target);
    if (target->IsDirectory()) {
        // The target must not be a directory
        return MX_ERR_NOT_FILE;
    }

    // The destination should not exist
    DirArgs args = DirArgs();
    args.name = name;
    args.len = len;
    mx_status_t status;
    if ((status = ForEachDirent(&args, cb_dir_find)) != MX_ERR_NOT_FOUND) {
        return (status == MX_OK) ? MX_ERR_ALREADY_EXISTS : status;
    }

    WriteTxn txn(fs_->bc_.get());
    args.ino = target->ino_;
    args.type = kMinfsTypeFile; // We can't hard link directories
    args.reclen = static_cast<uint32_t>(DirentSize(static_cast<uint8_t>(len)));
    args.txn = &txn;
    if ((status = ForEachDirent(&args, cb_dir_append)) < 0) {
        return status;
    }

    // We have successfully added the vn to a new location. Increment the link count.
    target->inode_.link_count++;
    target->InodeSync(&txn, kMxFsSyncDefault);

    return MX_OK;
}

mx_status_t VnodeMinfs::Sync() {
    return fs_->bc_->Sync();
}

#ifdef __Fuchsia__
mx_status_t VnodeMinfs::AttachRemote(fs::MountChannel h) {
    if (!IsDirectory() || IsDeletedDirectory()) {
        return MX_ERR_NOT_DIR;
    } else if (IsRemote()) {
        return MX_ERR_ALREADY_BOUND;
    }
    SetRemote(fbl::move(h.TakeChannel()));
    return MX_OK;
}
#endif

} // namespace minfs
