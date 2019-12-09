// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <zircon/device/vfs.h>
#include <zircon/time.h>

#include <memory>

#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/string_piece.h>
#include <fs/vfs_types.h>
#include <safemath/checked_math.h>

#ifdef __Fuchsia__
#include <lib/fdio/vfs.h>
#include <lib/fidl-utils/bind.h>
#include <zircon/syscalls.h>

#include <utility>

#include <fbl/auto_lock.h>
#endif

#include "directory.h"
#include "file.h"
#include "minfs-private.h"
#include "vnode.h"

namespace minfs {

#ifdef __Fuchsia__

void VnodeMinfs::HandleFsSpecificMessage(fidl_msg_t* msg, fidl::Transaction* transaction) {
  llcpp::fuchsia::minfs::Minfs::Dispatch(this, msg, transaction);
}

#endif  // __Fuchsia__

void VnodeMinfs::SetIno(ino_t ino) {
  ZX_DEBUG_ASSERT(ino_ == 0);
  ino_ = ino;
}

void VnodeMinfs::InodeSync(PendingWork* transaction, uint32_t flags) {
  // by default, c/mtimes are not updated to current time
  if (flags != kMxFsSyncDefault) {
    zx_time_t cur_time = GetTimeUTC();
    // update times before syncing
    if ((flags & kMxFsSyncMtime) != 0) {
      inode_.modify_time = cur_time;
    }
    if ((flags & kMxFsSyncCtime) != 0) {
      inode_.create_time = cur_time;
    }
  }

  fs_->InodeUpdate(transaction, ino_, &inode_);
}

// Delete all blocks (relative to a file) from "start" (inclusive) to the end of
// the file. Does not update mtime/atime.
zx_status_t VnodeMinfs::BlocksShrink(Transaction* transaction, blk_t start) {
  ZX_DEBUG_ASSERT(transaction != nullptr);

  auto block_callback = [this, transaction](blk_t local_bno, blk_t old_bno, blk_t* out_bno) {
    DeleteBlock(transaction, local_bno, old_bno);
    *out_bno = 0;
  };

  BlockOpArgs op_args(transaction, BlockOp::kDelete, std::move(block_callback), start,
                      static_cast<blk_t>(kMinfsMaxFileBlock - start), nullptr);
  zx_status_t status;
  if ((status = ApplyOperation(&op_args)) != ZX_OK) {
    return status;
  }

#ifdef __Fuchsia__
  // Arbitrary minimum size for indirect vmo
  size_t size = (kMinfsIndirect + kMinfsDoublyIndirect) * kMinfsBlockSize;
  // Number of blocks before dindirect blocks start
  blk_t pre_dindirect = kMinfsDirect + kMinfsDirectPerIndirect * kMinfsIndirect;
  if (start > pre_dindirect) {
    blk_t distart = start - pre_dindirect;  // first bno relative to dindirect blocks
    blk_t last_dindirect = distart / (kMinfsDirectPerDindirect);  // index of last dindirect

    // Calculate new size for indirect vmo
    if (distart % kMinfsDirectPerDindirect) {
      size = GetVmoSizeForIndirect(last_dindirect);
    } else if (last_dindirect) {
      size = GetVmoSizeForIndirect(last_dindirect - 1);
    }
  }

  // Shrink the indirect vmo if necessary
  if (vmo_indirect_ != nullptr && vmo_indirect_->size() > size) {
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

  if (vmo_indirect_->size() < size) {
    zx_status_t status;
    if ((status = vmo_indirect_->Grow(size)) != ZX_OK) {
      return status;
    }
  }

  fs::ReadTxn read_transaction(fs_->bc_.get());

  for (uint32_t i = 0; i < count; i++) {
    blk_t ibno;
    if ((ibno = iarray[i]) != 0) {
      fs_->ValidateBno(ibno);
      read_transaction.Enqueue(vmoid_indirect_.id, offset + i, ibno + fs_->Info().dat_block, 1);
    }
  }

  return read_transaction.Transact();
}

zx_status_t VnodeMinfs::LoadIndirectWithinDoublyIndirect(uint32_t dindex) {
  uint32_t* dientry;

  size_t size = GetVmoSizeForIndirect(dindex);
  if (vmo_indirect_->size() >= size) {
    // We've already loaded this indirect (within dind) block.
    return ZX_OK;
  }

  ReadIndirectVmoBlock(GetVmoOffsetForDoublyIndirect(dindex), &dientry);
  return LoadIndirectBlocks(dientry, kMinfsDirectPerIndirect, GetVmoOffsetForIndirect(dindex),
                            size);
}

zx_status_t VnodeMinfs::InitIndirectVmo() {
  if (vmo_indirect_ != nullptr) {
    return ZX_OK;
  }

  vmo_indirect_ = fzl::ResizeableVmoMapper::Create(
      kMinfsBlockSize * (kMinfsIndirect + kMinfsDoublyIndirect), "minfs-indirect");

  zx_status_t status = fs_->bc_->device()->BlockAttachVmo(vmo_indirect_->vmo(), &vmoid_indirect_);
  if (status != ZX_OK) {
    vmo_indirect_ = nullptr;
    return status;
  }

  // Load initial set of indirect blocks
  if ((status = LoadIndirectBlocks(inode_.inum, kMinfsIndirect, 0, 0)) != ZX_OK) {
    vmo_indirect_ = nullptr;
    return status;
  }

  // Load doubly indirect blocks
  if ((status =
           LoadIndirectBlocks(inode_.dinum, kMinfsDoublyIndirect, GetVmoOffsetForDoublyIndirect(0),
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
zx_status_t VnodeMinfs::InitVmo(PendingWork* transaction) {
  if (vmo_.is_valid()) {
    return ZX_OK;
  }

  zx_status_t status;
  const size_t vmo_size = fbl::round_up(GetSize(), kMinfsBlockSize);
  if ((status = zx::vmo::create(vmo_size, ZX_VMO_RESIZABLE, &vmo_)) != ZX_OK) {
    FS_TRACE_ERROR("Failed to initialize vmo; error: %d\n", status);
    return status;
  }
  vmo_size_ = vmo_size;

  zx_object_set_property(vmo_.get(), ZX_PROP_NAME, "minfs-inode", 11);

  if ((status = fs_->bc_->device()->BlockAttachVmo(vmo_, &vmoid_)) != ZX_OK) {
    vmo_.reset();
    return status;
  }
  fs::ReadTxn read_transaction(fs_->bc_.get());
  uint32_t dnum_count = 0;
  uint32_t inum_count = 0;
  uint32_t dinum_count = 0;
  fs::Ticker ticker(fs_->StartTicker());
  auto get_metrics = fbl::MakeAutoCall([&]() {
    fs_->UpdateInitMetrics(dnum_count, inum_count, dinum_count, vmo_size, ticker.End());
  });

  // Initialize all direct blocks
  blk_t bno;
  for (uint32_t d = 0; d < kMinfsDirect; d++) {
    if ((bno = inode_.dnum[d]) != 0) {
      fs_->ValidateBno(bno);
      dnum_count++;
      read_transaction.Enqueue(vmoid_.id, d, bno + fs_->Info().dat_block, 1);
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
          read_transaction.Enqueue(vmoid_.id, n, bno + fs_->Info().dat_block, 1);
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
              uint32_t n = kMinfsDirect + kMinfsIndirect * kMinfsDirectPerIndirect +
                           j * kMinfsDirectPerIndirect + k;
              read_transaction.Enqueue(vmoid_.id, n, bno + fs_->Info().dat_block, 1);
            }
          }
        }
      }
    }
  }

  status = read_transaction.Transact();
  ValidateVmoTail(GetSize());
  return status;
}
#endif

void VnodeMinfs::AllocateIndirect(Transaction* transaction, blk_t index, IndirectArgs* args) {
  ZX_DEBUG_ASSERT(transaction != nullptr);

  // *bno must not be already allocated
  ZX_DEBUG_ASSERT(args->GetBno(index) == 0);

  // allocate new indirect block
  blk_t bno;
  fs_->BlockNew(transaction, &bno);

#ifdef __Fuchsia__
  ClearIndirectVmoBlock(args->GetOffset() + index);
#else
  ClearIndirectBlock(bno);
#endif

  args->SetBno(index, bno);
  inode_.block_count++;
}

zx_status_t VnodeMinfs::BlockOpDirect(BlockOpArgs* op_args, DirectArgs* params) {
  for (unsigned i = 0; i < params->GetCount(); i++) {
    blk_t bno = params->GetBno(i);
    op_args->callback(params->GetRelativeBlock() + i, bno, &bno);
    params->SetBno(i, bno);
  }
  return ZX_OK;
}

zx_status_t VnodeMinfs::BlockOpIndirect(BlockOpArgs* op_args, IndirectArgs* params) {
  // we should have initialized vmo before calling this method
  zx_status_t status;

#ifdef __Fuchsia__
  if (params->GetOp() != BlockOp::kDelete) {
    ValidateVmoSize(vmo_indirect_->vmo().get(), params->GetOffset() + params->GetCount());
  }
#endif

  for (unsigned i = 0; i < params->GetCount(); i++) {
    // If the indirect block is newly allocated, we must write an empty block out to disk.
    bool allocated = false;
    if (params->GetBno(i) == 0) {
      switch (params->GetOp()) {
        case BlockOp::kDelete:
          continue;
        case BlockOp::kRead:
          continue;
        case BlockOp::kSwap:
          __FALLTHROUGH;
        case BlockOp::kWrite:
          AllocateIndirect(op_args->transaction, i, params);
          allocated = true;
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
    if ((status = BlockOpDirect(op_args, &direct_params)) != ZX_OK) {
      return status;
    }

    // We can delete the current indirect block if all direct blocks within it are deleted
    if (params->GetOp() == BlockOp::kDelete &&
        direct_params.GetCount() == kMinfsDirectPerIndirect) {
      // release the indirect block itself
      fs_->BlockFree(op_args->transaction, params->GetBno(i));
      params->SetBno(i, 0);
      inode_.block_count--;
    } else if (allocated || direct_params.IsDirty()) {
      // Only update the indirect block if an entry was deleted, and the indirect block
      // itself was not deleted.
#ifdef __Fuchsia__
      storage::Operation op = {
          .type = storage::OperationType::kWrite,
          .vmo_offset = params->GetOffset() + i,
          .dev_offset = params->GetBno(i) + fs_->Info().dat_block,
          .length = 1,
      };
      op_args->transaction->EnqueueMetadata(vmo_indirect_->vmo().get(), std::move(op));
#else
      fs_->bc_->Writeblk(params->GetBno(i) + fs_->Info().dat_block, entry);
#endif
      params->SetDirty();
    }
  }

  return ZX_OK;
}

zx_status_t VnodeMinfs::BlockOpDindirect(BlockOpArgs* op_args, DindirectArgs* params) {
  zx_status_t status;

#ifdef __Fuchsia__
  if (params->GetOp() != BlockOp::kDelete) {
    ValidateVmoSize(vmo_indirect_->vmo().get(), params->GetOffset() + params->GetCount());
  }
#endif

  // operate on doubly indirect blocks
  for (unsigned i = 0; i < params->GetCount(); i++) {
    // If the indirect block is newly allocated, we must write an empty block out to disk.
    bool allocated = false;
    if (params->GetBno(i) == 0) {
      switch (params->GetOp()) {
        case BlockOp::kDelete:
          continue;
        case BlockOp::kRead:
          continue;
        case BlockOp::kSwap:
          __FALLTHROUGH;
        case BlockOp::kWrite:
          AllocateIndirect(op_args->transaction, i, params);
          allocated = true;
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
    if ((status = BlockOpIndirect(op_args, &indirect_params)) != ZX_OK) {
      return status;
    }

    // We can delete the current doubly indirect block if all indirect blocks within it
    // (and direct blocks within those) are deleted
    if (params->GetOp() == BlockOp::kDelete &&
        indirect_params.GetCount() == kMinfsDirectPerDindirect) {
      // release the doubly indirect block itself
      fs_->BlockFree(op_args->transaction, params->GetBno(i));
      params->SetBno(i, 0);
      inode_.block_count--;
    } else if (allocated || indirect_params.IsDirty()) {
      // Only update the indirect block if an entry was deleted, and the indirect block
      // itself was not deleted.
#ifdef __Fuchsia__
      storage::Operation op = {
          .type = storage::OperationType::kWrite,
          .vmo_offset = params->GetOffset() + i,
          .dev_offset = params->GetBno(i) + fs_->Info().dat_block,
          .length = 1,
      };
      op_args->transaction->EnqueueMetadata(vmo_indirect_->vmo().get(), std::move(op));
#else
      fs_->bc_->Writeblk(params->GetBno(i) + fs_->Info().dat_block, dientry);
#endif
      params->SetDirty();
    }
  }

  return ZX_OK;
}

#ifdef __Fuchsia__
void VnodeMinfs::ReadIndirectVmoBlock(uint32_t offset, uint32_t** entry) {
  ZX_DEBUG_ASSERT(vmo_indirect_ != nullptr);
  uintptr_t addr = reinterpret_cast<uintptr_t>(vmo_indirect_->start());
  ValidateVmoSize(vmo_indirect_->vmo().get(), offset);
  *entry = reinterpret_cast<uint32_t*>(addr + kMinfsBlockSize * offset);
}

void VnodeMinfs::ClearIndirectVmoBlock(uint32_t offset) {
  ZX_DEBUG_ASSERT(vmo_indirect_ != nullptr);
  uintptr_t addr = reinterpret_cast<uintptr_t>(vmo_indirect_->start());
  ValidateVmoSize(vmo_indirect_->vmo().get(), offset);
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

zx_status_t VnodeMinfs::ApplyOperation(BlockOpArgs* op_args) {
  blk_t start = op_args->start;
  blk_t found = 0;
  bool dirty = false;
  if (found < op_args->count && start < kMinfsDirect) {
    // array starting with first direct block
    blk_t* array = &inode_.dnum[start];
    // number of direct blocks to process
    blk_t count = fbl::min(op_args->count - found, kMinfsDirect - start);
    // if bnos exist, adjust past found (should be 0)
    blk_t* bnos = op_args->bnos == nullptr ? nullptr : &op_args->bnos[found];

    DirectArgs direct_params(op_args->op, array, count, op_args->start, bnos);
    zx_status_t status;
    if ((status = BlockOpDirect(op_args, &direct_params)) != ZX_OK) {
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

  if (found < op_args->count && start < kMinfsIndirect * kMinfsDirectPerIndirect) {
    // index of indirect block, and offset of that block within indirect vmo
    blk_t ibindex = start / kMinfsDirectPerIndirect;
    // index of direct block within indirect block
    blk_t bindex = start % kMinfsDirectPerIndirect;

    // array starting with first indirect block
    blk_t* array = &inode_.inum[ibindex];
    // number of direct blocks to process within indirect blocks
    blk_t count =
        fbl::min(op_args->count - found, kMinfsIndirect * kMinfsDirectPerIndirect - start);
    // if bnos exist, adjust past found
    blk_t* bnos = op_args->bnos == nullptr ? nullptr : &op_args->bnos[found];

    IndirectArgs indirect_params(op_args->op, array, count, op_args->start + found, bnos, bindex,
                                 ibindex);
    zx_status_t status;
    if ((status = BlockOpIndirect(op_args, &indirect_params)) != ZX_OK) {
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

  if (found < op_args->count &&
      start < kMinfsDoublyIndirect * kMinfsDirectPerIndirect * kMinfsDirectPerIndirect) {
    // index of doubly indirect block
    uint32_t dibindex = start / (kMinfsDirectPerIndirect * kMinfsDirectPerIndirect);
    ZX_DEBUG_ASSERT(dibindex < kMinfsDoublyIndirect);
    start -= (dibindex * kMinfsDirectPerIndirect * kMinfsDirectPerIndirect);

    // array starting with first doubly indirect block
    blk_t* array = &inode_.dinum[dibindex];
    // number of direct blocks to process within doubly indirect blocks
    blk_t count =
        fbl::min(op_args->count - found,
                 kMinfsDoublyIndirect * kMinfsDirectPerIndirect * kMinfsDirectPerIndirect - start);
    // if bnos exist, adjust past found
    blk_t* bnos = op_args->bnos == nullptr ? nullptr : &op_args->bnos[found];
    // index of direct block within indirect block
    blk_t bindex = start % kMinfsDirectPerIndirect;
    // offset of indirect block within indirect vmo
    blk_t ib_vmo_offset = GetVmoOffsetForIndirect(dibindex);
    // index of indirect block within doubly indirect block
    blk_t ibindex = start / kMinfsDirectPerIndirect;
    // offset of doubly indirect block within indirect vmo
    blk_t dib_vmo_offset = GetVmoOffsetForDoublyIndirect(dibindex);

    DindirectArgs dindirect_params(op_args->op, array, count, op_args->start + found, bnos, bindex,
                                   ib_vmo_offset, ibindex, dib_vmo_offset);
    zx_status_t status;
    if ((status = BlockOpDindirect(op_args, &dindirect_params)) != ZX_OK) {
      return status;
    }

    found += count;
    dirty |= dindirect_params.IsDirty();
  }

  if (dirty) {
    ZX_DEBUG_ASSERT(op_args->transaction != nullptr);
    InodeSync(op_args->transaction, kMxFsSyncDefault);
  }

  // Return out of range if we were not able to process all blocks
  return found == op_args->count ? ZX_OK : ZX_ERR_OUT_OF_RANGE;
}

zx_status_t VnodeMinfs::EnsureIndirectVmoSize(blk_t n) {
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
      if (vmo_indirect_->size() < vmo_size) {
        if ((status = vmo_indirect_->Grow(vmo_size)) != ZX_OK) {
          return status;
        }
      }
    }
  }
#endif
  return ZX_OK;
}

zx_status_t VnodeMinfs::BlockGetWritable(Transaction* transaction, blk_t n, blk_t* bno) {
  zx_status_t status = EnsureIndirectVmoSize(n);
  if (status != ZX_OK) {
    return ZX_OK;
  }

  auto block_callback = [this, transaction](blk_t local_bno, blk_t old_bno, blk_t* out_bno) {
    AcquireWritableBlock(transaction, local_bno, old_bno, out_bno);
  };
  BlockOpArgs op_args(transaction, BlockOp::kWrite, std::move(block_callback), n, 1, bno);
  return ApplyOperation(&op_args);
}

zx_status_t VnodeMinfs::BlockGetReadable(blk_t n, blk_t* bno) {
  zx_status_t status = EnsureIndirectVmoSize(n);
  if (status != ZX_OK) {
    return ZX_OK;
  }

  // Just acquire the old values.
  auto block_callback = [](blk_t local_bno, blk_t old_bno, blk_t* out_bno) {};

  BlockOpArgs op_args(nullptr, BlockOp::kRead, std::move(block_callback), n, 1, bno);
  return ApplyOperation(&op_args);
}

zx_status_t VnodeMinfs::ReadExactInternal(PendingWork* transaction, void* data, size_t len,
                                          size_t off) {
  size_t actual;
  zx_status_t status = ReadInternal(transaction, data, len, off, &actual);
  if (status != ZX_OK) {
    return status;
  } else if (actual != len) {
    return ZX_ERR_IO;
  }
  return ZX_OK;
}

zx_status_t VnodeMinfs::WriteExactInternal(Transaction* transaction, const void* data, size_t len,
                                           size_t off) {
  size_t actual;
  zx_status_t status = WriteInternal(transaction, data, len, off, &actual);
  if (status != ZX_OK) {
    return status;
  } else if (actual != len) {
    return ZX_ERR_IO;
  }
  InodeSync(transaction, kMxFsSyncMtime);
  return ZX_OK;
}

void VnodeMinfs::RemoveInodeLink(PendingWork* transaction) {
  ZX_ASSERT(inode_.link_count > 0);

  // This effectively 'unlinks' the target node without deleting the direntry
  inode_.link_count--;
  if (IsDirectory()) {
    if (inode_.link_count == 1) {
      // Directories are initialized with two links, since they point
      // to themselves via ".". Thus, when they reach "one link", they
      // are only pointed to by themselves, and should be deleted.
      inode_.link_count--;
    }
  }

  if (IsUnlinked()) {
    if (fd_count_ == 0) {
      Purge(transaction);
    } else {
      fs_->AddUnlinked(transaction, this);
    }
  }

  InodeSync(transaction, kMxFsSyncMtime);
}

void VnodeMinfs::ValidateVmoTail(uint64_t inode_size) const {
#if defined(MINFS_PARANOID_MODE) && defined(__Fuchsia__)
  if (!vmo_.is_valid()) {
    return;
  }

  // Verify that everything not allocated to "inode_size" in the
  // last block is filled with zeroes.
  char buf[kMinfsBlockSize];
  const size_t vmo_size = fbl::round_up(inode_size, kMinfsBlockSize);
  ZX_ASSERT(vmo_.read(buf, inode_size, vmo_size - inode_size) == ZX_OK);
  for (size_t i = 0; i < vmo_size - inode_size; i++) {
    ZX_ASSERT_MSG(buf[i] == 0, "vmo[%" PRIu64 "] != 0 (inode size = %u)\n", inode_size + i,
                  inode_size);
  }
#endif  // MINFS_PARANOID_MODE && __Fuchsia__
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
    request[request_count].vmoid = vmoid_.id;
    request[request_count].opcode = BLOCKIO_CLOSE_VMO;
    request_count++;
  }
  if (vmo_indirect_ != nullptr) {
    request[request_count].group = fs_->bc_->BlockGroupID();
    request[request_count].vmoid = vmoid_indirect_.id;
    request[request_count].opcode = BLOCKIO_CLOSE_VMO;
    request_count++;
  }
  if (request_count) {
    fs_->bc_->Transaction(&request[0], request_count);
  }
#endif
}

zx_status_t VnodeMinfs::Open([[maybe_unused]] ValidatedOptions options,
                             fbl::RefPtr<Vnode>* out_redirect) {
  fd_count_++;
  return ZX_OK;
}

void VnodeMinfs::Purge(PendingWork* transaction) {
  ZX_DEBUG_ASSERT(fd_count_ == 0);
  ZX_DEBUG_ASSERT(IsUnlinked());
  fs_->VnodeRelease(this);
#ifdef __Fuchsia__
  // TODO(smklein): Only init indirect vmo if it's needed
  if (InitIndirectVmo() == ZX_OK) {
    fs_->InoFree(transaction, this);
  } else {
    FS_TRACE_ERROR("minfs: Failed to Init Indirect VMO while purging %u\n", ino_);
  }
#else
  fs_->InoFree(transaction, this);
#endif
}

zx_status_t VnodeMinfs::Close() {
  ZX_DEBUG_ASSERT_MSG(fd_count_ > 0, "Closing ino with no fds open");
  fd_count_--;

  if (fd_count_ == 0 && IsUnlinked()) {
    zx_status_t status;
    std::unique_ptr<Transaction> transaction;
    if ((status = fs_->BeginTransaction(0, 0, &transaction)) != ZX_OK) {
      return status;
    }
    fs_->RemoveUnlinked(transaction.get(), this);
    Purge(transaction.get());
    fs_->CommitTransaction(std::move(transaction));
  }
  return ZX_OK;
}

// Internal read. Usable on directories.
zx_status_t VnodeMinfs::ReadInternal(PendingWork* transaction, void* data, size_t len, size_t off,
                                     size_t* actual) {
  // clip to EOF
  if (off >= GetSize()) {
    *actual = 0;
    return ZX_OK;
  }
  if (len > (GetSize() - off)) {
    len = GetSize() - off;
  }

  zx_status_t status;
#ifdef __Fuchsia__
  if ((status = InitVmo(transaction)) != ZX_OK) {
    return status;
  } else if ((status = vmo_.read(data, off, len)) != ZX_OK) {
    return status;
  } else {
    *actual = len;
  }
#else
  void* start = data;
  uint32_t n = static_cast<uint32_t>(off / kMinfsBlockSize);
  size_t adjust = off % kMinfsBlockSize;

  while ((len > 0) && (n < kMinfsMaxFileBlock)) {
    size_t xfer;
    if (len > (kMinfsBlockSize - adjust)) {
      xfer = kMinfsBlockSize - adjust;
    } else {
      xfer = len;
    }

    blk_t bno;
    if ((status = BlockGetReadable(n, &bno)) != ZX_OK) {
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

// Internal write. Usable on directories.
zx_status_t VnodeMinfs::WriteInternal(Transaction* transaction, const void* data, size_t len,
                                      size_t off, size_t* actual) {
  if (len == 0) {
    *actual = 0;
    return ZX_OK;
  }
  zx_status_t status;
#ifdef __Fuchsia__
  // TODO(planders): Once we are splitting up write transactions, assert this on host as well.
  ZX_DEBUG_ASSERT(len < TransactionLimits::kMaxWriteBytes);
  if ((status = InitVmo(transaction)) != ZX_OK) {
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
    if ((xfer_off + xfer) > vmo_size_) {
      size_t new_size = fbl::round_up(xfer_off + xfer, kMinfsBlockSize);
      ZX_DEBUG_ASSERT(new_size >= GetSize());  // Overflow.
      if ((status = vmo_.set_size(new_size)) != ZX_OK) {
        break;
      }
      vmo_size_ = new_size;
    }

    // Update this block of the in-memory VMO
    if ((status = vmo_.write(data, xfer_off, xfer)) != ZX_OK) {
      break;
    }

    // Update this block on-disk
    blk_t bno;
    if ((status = BlockGetWritable(transaction, n, &bno))) {
      break;
    }

    IssueWriteback(transaction, n, bno + fs_->Info().dat_block, 1);
#else   // __Fuchsia__
    blk_t bno;
    if ((status = BlockGetWritable(transaction, n, &bno))) {
      break;
    }
    ZX_DEBUG_ASSERT(bno != 0);
    char wdata[kMinfsBlockSize];
    if (fs_->bc_->Readblk(bno + fs_->Info().dat_block, wdata)) {
      break;
    }
    memcpy(wdata + adjust, data, xfer);
    if (len < kMinfsBlockSize && max_size >= GetSize()) {
      memset(wdata + adjust + xfer, 0, kMinfsBlockSize - (adjust + xfer));
    }
    if (fs_->bc_->Writeblk(bno + fs_->Info().dat_block, wdata)) {
      break;
    }
#endif  // __Fuchsia__

    adjust = 0;
    len -= xfer;
    data = (void*)((uintptr_t)(data) + xfer);
    n++;
  }

  len = (uintptr_t)data - (uintptr_t)start;
  if (len == 0) {
    // If more than zero bytes were requested, but zero bytes were written,
    // return an error explicitly (rather than zero).
    if (off >= kMinfsMaxFileSize) {
      return ZX_ERR_FILE_BIG;
    }

    return ZX_ERR_NO_SPACE;
  }

  if ((off + len) > GetSize()) {
    SetSize(static_cast<uint32_t>(off + len));
  }

  *actual = len;

  ValidateVmoTail(GetSize());
  return ZX_OK;
}

zx_status_t VnodeMinfs::GetAttributes(fs::VnodeAttributes* a) {
  FS_TRACE_DEBUG("minfs_getattr() vn=%p(#%u)\n", this, ino_);
  // This transaction exists because acquiring the block size and block
  // count may be unsafe without locking.
  //
  // TODO: Improve locking semantics of pending data allocation to make this less confusing.
  Transaction transaction(fs_);
  *a = fs::VnodeAttributes();
  a->mode = DTYPE_TO_VTYPE(MinfsMagicType(inode_.magic)) | V_IRUSR | V_IWUSR | V_IRGRP | V_IROTH;
  a->inode = ino_;
  a->content_size = GetSize();
  a->storage_size = GetBlockCount() * kMinfsBlockSize;
  a->link_count = inode_.link_count;
  a->creation_time = inode_.create_time;
  a->modification_time = inode_.modify_time;
  return ZX_OK;
}

zx_status_t VnodeMinfs::SetAttributes(fs::VnodeAttributesUpdate attr) {
  int dirty = 0;
  FS_TRACE_DEBUG("minfs_setattr() vn=%p(#%u)\n", this, ino_);
  if (attr.has_creation_time()) {
    inode_.create_time = attr.take_creation_time();
    dirty = 1;
  }
  if (attr.has_modification_time()) {
    inode_.modify_time = attr.take_modification_time();
    dirty = 1;
  }
  if (attr.any()) {
    // any unhandled field update is unsupported
    return ZX_ERR_INVALID_ARGS;
  }
  if (dirty) {
    // write to disk, but don't overwrite the time
    zx_status_t status;
    std::unique_ptr<Transaction> transaction;
    if ((status = fs_->BeginTransaction(0, 0, &transaction)) != ZX_OK) {
      return status;
    }
    InodeSync(transaction.get(), kMxFsSyncDefault);
    transaction->PinVnode(fbl::RefPtr(this));
    fs_->CommitTransaction(std::move(transaction));
  }
  return ZX_OK;
}

VnodeMinfs::VnodeMinfs(Minfs* fs) : fs_(fs) {}

#ifdef __Fuchsia__
void VnodeMinfs::Notify(fbl::StringPiece name, unsigned event) { watcher_.Notify(name, event); }
zx_status_t VnodeMinfs::WatchDir(fs::Vfs* vfs, uint32_t mask, uint32_t options,
                                 zx::channel watcher) {
  return watcher_.WatchDir(vfs, this, mask, options, std::move(watcher));
}

#endif

void VnodeMinfs::Allocate(Minfs* fs, uint32_t type, fbl::RefPtr<VnodeMinfs>* out) {
  if (type == kMinfsTypeDir) {
    *out = fbl::AdoptRef(new Directory(fs));
  } else {
    *out = fbl::AdoptRef(new File(fs));
  }
  memset(&(*out)->inode_, 0, sizeof((*out)->inode_));
  (*out)->inode_.magic = MinfsMagic(type);
  (*out)->inode_.create_time = (*out)->inode_.modify_time = GetTimeUTC();
  if (type == kMinfsTypeDir) {
    (*out)->inode_.link_count = 2;
    // "." and "..".
    (*out)->inode_.dirent_count = 2;
  } else {
    (*out)->inode_.link_count = 1;
  }
}

void VnodeMinfs::Recreate(Minfs* fs, ino_t ino, fbl::RefPtr<VnodeMinfs>* out) {
  Inode inode;
  fs->InodeLoad(ino, &inode);
  if (inode.magic == kMinfsMagicDir) {
    *out = fbl::AdoptRef(new Directory(fs));
  } else {
    *out = fbl::AdoptRef(new File(fs));
  }
  memcpy(&(*out)->inode_, &inode, sizeof(inode));

  (*out)->ino_ = ino;
  (*out)->SetSize((*out)->inode_.size);
}

#ifdef __Fuchsia__

constexpr const char kFsName[] = "minfs";

zx_status_t VnodeMinfs::QueryFilesystem(::llcpp::fuchsia::io::FilesystemInfo* info) {
  static_assert(fbl::constexpr_strlen(kFsName) + 1 < ::llcpp::fuchsia::io::MAX_FS_NAME_BUFFER,
                "Minfs name too long");
  Transaction transaction(fs_);
  *info = {};
  info->block_size = kMinfsBlockSize;
  info->max_filename_size = kMinfsMaxNameSize;
  info->fs_type = VFS_TYPE_MINFS;
  info->fs_id = fs_->GetFsId();
  info->total_bytes = fs_->Info().block_count * fs_->Info().block_size;
  info->used_bytes = fs_->Info().alloc_block_count * fs_->Info().block_size;
  info->total_nodes = fs_->Info().inode_count;
  info->used_nodes = fs_->Info().alloc_inode_count;

  fuchsia_hardware_block_volume_VolumeInfo fvm_info;
  if (fs_->FVMQuery(&fvm_info) == ZX_OK) {
    uint64_t free_slices = fvm_info.pslice_total_count - fvm_info.pslice_allocated_count;
    info->free_shared_pool_bytes = fvm_info.slice_size * free_slices;
  }

  strlcpy(reinterpret_cast<char*>(info->name.data()), kFsName,
          ::llcpp::fuchsia::io::MAX_FS_NAME_BUFFER);
  return ZX_OK;
}

zx_status_t VnodeMinfs::GetDevicePath(size_t buffer_len, char* out_name, size_t* out_len) {
  return fs_->bc_->device()->GetDevicePath(buffer_len, out_name, out_len);
}

void VnodeMinfs::GetMetrics(GetMetricsCompleter::Sync completer) {
  ::llcpp::fuchsia::minfs::Metrics metrics;
  zx_status_t status = fs_->GetMetrics(&metrics);
  completer.Reply(status, status == ZX_OK ? &metrics : nullptr);
}

void VnodeMinfs::ToggleMetrics(bool enable, ToggleMetricsCompleter::Sync completer) {
  fs_->SetMetrics(enable);
  completer.Reply(ZX_OK);
}

void VnodeMinfs::GetAllocatedRegions(GetAllocatedRegionsCompleter::Sync completer) {
  static_assert(sizeof(llcpp::fuchsia::minfs::BlockRegion) == sizeof(BlockRegion));
  static_assert(offsetof(llcpp::fuchsia::minfs::BlockRegion, offset) ==
                offsetof(BlockRegion, offset));
  static_assert(offsetof(llcpp::fuchsia::minfs::BlockRegion, length) ==
                offsetof(BlockRegion, length));
  zx::vmo vmo;
  zx_status_t status = ZX_OK;
  fbl::Vector<BlockRegion> buffer = fs_->GetAllocatedRegions();
  uint64_t allocations = buffer.size();
  if (allocations != 0) {
    status = zx::vmo::create(sizeof(BlockRegion) * allocations, 0, &vmo);
    if (status == ZX_OK) {
      status = vmo.write(buffer.data(), 0, sizeof(BlockRegion) * allocations);
    }
  }
  if (status == ZX_OK) {
    completer.Reply(ZX_OK, std::move(vmo), allocations);
  } else {
    completer.Reply(status, zx::vmo(), 0);
  };
}

#endif

zx_status_t VnodeMinfs::TruncateInternal(Transaction* transaction, size_t len) {
  zx_status_t status = ZX_OK;
#ifdef __Fuchsia__
  // TODO(smklein): We should only init up to 'len'; no need
  // to read in the portion of a large file we plan on deleting.
  if ((status = InitVmo(transaction)) != ZX_OK) {
    FS_TRACE_ERROR("minfs: Truncate failed to initialize VMO: %d\n", status);
    return ZX_ERR_IO;
  }
#endif

  uint64_t inode_size = GetSize();
  if (len < inode_size) {
    // Truncate should make the file shorter.
    blk_t bno = safemath::checked_cast<blk_t>(inode_size / kMinfsBlockSize);

    // Truncate to the nearest block.
    blk_t trunc_bno = static_cast<blk_t>(len / kMinfsBlockSize);
    // [start_bno, EOF) blocks should be deleted entirely.
    blk_t start_bno = static_cast<blk_t>((len % kMinfsBlockSize == 0) ? trunc_bno : trunc_bno + 1);

    if ((status = BlocksShrink(transaction, start_bno)) != ZX_OK) {
      return status;
    }

#ifdef __Fuchsia__
    uint64_t decommit_offset = fbl::round_up(len, kMinfsBlockSize);
    uint64_t decommit_length = fbl::round_up(inode_size, kMinfsBlockSize) - decommit_offset;
    if (decommit_length > 0) {
      status = vmo_.op_range(ZX_VMO_OP_DECOMMIT, decommit_offset, decommit_length, nullptr, 0);
      if (status != ZX_OK) {
        // TODO(35948): This is a known issue; the additional logging here is to help
        // diagnose.
        FS_TRACE_ERROR("TruncateInternal: Modifying node length from %zu to %zu\n", inode_size,
                       len);
        FS_TRACE_ERROR("  Decommit from offset %zu, length %zu. Status: %d\n", decommit_offset,
                       decommit_length, status);
        ZX_ASSERT(status == ZX_OK);
      }
    }
#endif
    // Shrink the size to be block-aligned if we are removing blocks from
    // the end of the vnode.
    if (start_bno * kMinfsBlockSize < inode_size) {
      SetSize(start_bno * kMinfsBlockSize);
    }

    // Write zeroes to the rest of the remaining block, if it exists
    if (len < GetSize()) {
      char bdata[kMinfsBlockSize];
      blk_t rel_bno = static_cast<blk_t>(len / kMinfsBlockSize);
      bno = 0;
      if ((status = BlockGetReadable(rel_bno, &bno)) != ZX_OK) {
        FS_TRACE_ERROR("minfs: Truncate failed to get block %u of file: %d\n", rel_bno, status);
        return ZX_ERR_IO;
      }

      size_t adjust = len % kMinfsBlockSize;
#ifdef __Fuchsia__
      bool allocated = (bno != 0);
      if (allocated || HasPendingAllocation(rel_bno)) {
        if ((status = vmo_.read(bdata, len - adjust, adjust)) != ZX_OK) {
          FS_TRACE_ERROR("minfs: Truncate failed to read last block: %d\n", status);
          return ZX_ERR_IO;
        }
        memset(bdata + adjust, 0, kMinfsBlockSize - adjust);

        if ((status = vmo_.write(bdata, len - adjust, kMinfsBlockSize)) != ZX_OK) {
          FS_TRACE_ERROR("minfs: Truncate failed to write last block: %d\n", status);
          return ZX_ERR_IO;
        }

        if ((status = BlockGetWritable(transaction, rel_bno, &bno)) != ZX_OK) {
          FS_TRACE_ERROR("minfs: Truncate failed to get block %u of file: %d\n", rel_bno, status);
          return ZX_ERR_IO;
        }
        IssueWriteback(transaction, rel_bno, bno + fs_->Info().dat_block, 1);
      }
#else   // __Fuchsia__
      if (bno != 0) {
        if (fs_->bc_->Readblk(bno + fs_->Info().dat_block, bdata)) {
          return ZX_ERR_IO;
        }
        memset(bdata + adjust, 0, kMinfsBlockSize - adjust);
        if (fs_->bc_->Writeblk(bno + fs_->Info().dat_block, bdata)) {
          return ZX_ERR_IO;
        }
      }
#endif  // __Fuchsia__
    }
  } else if (len > inode_size) {
    // Truncate should make the file longer, filled with zeroes.
    if (kMinfsMaxFileSize < len) {
      return ZX_ERR_INVALID_ARGS;
    }
#ifdef __Fuchsia__
    uint64_t new_size = fbl::round_up(len, kMinfsBlockSize);
    if ((status = vmo_.set_size(new_size)) != ZX_OK) {
      return status;
    }
    vmo_size_ = new_size;
#endif
  } else {
    return ZX_OK;
  }

  // Setting the size does not ensure the on-disk inode is updated. Ensuring
  // writeback occurs is the responsibility of the caller.
  SetSize(static_cast<uint32_t>(len));
  ValidateVmoTail(GetSize());
  return ZX_OK;
}

#ifdef __Fuchsia__
zx_status_t VnodeMinfs::GetNodeInfoForProtocol([[maybe_unused]] fs::VnodeProtocol protocol,
                                               [[maybe_unused]] fs::Rights rights,
                                               fs::VnodeRepresentation* info) {
  if (IsDirectory()) {
    *info = fs::VnodeRepresentation::Directory();
  } else {
    *info = fs::VnodeRepresentation::File();
  }
  return ZX_OK;
}

void VnodeMinfs::Sync(SyncCallback closure) {
  TRACE_DURATION("minfs", "VnodeMinfs::Sync");
  fs_->Sync([this, cb = std::move(closure)](zx_status_t status) {
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
  SetRemote(h.TakeChannel());
  return ZX_OK;
}
#endif

VnodeMinfs::DirectArgs VnodeMinfs::IndirectArgs::GetDirect(blk_t* barray, blk_t ibindex) const {
  // Determine the starting index for direct blocks within this indirect block
  blk_t direct_start = ibindex == 0 ? bindex_ : 0;

  // Determine how many direct blocks have already been op'd in indirect block context
  blk_t offset = 0;

  if (ibindex) {
    offset = kMinfsDirectPerIndirect * ibindex - bindex_;
  }

  DirectArgs params(op_,                                                                // op
                    &barray[direct_start],                                              // array
                    fbl::min(count_ - offset, kMinfsDirectPerIndirect - direct_start),  // count
                    rel_bno_ + offset,                                                  // rel_bno
                    bnos_ == nullptr ? nullptr : &bnos_[offset]);                       // bnos
  return params;
}

VnodeMinfs::IndirectArgs VnodeMinfs::DindirectArgs::GetIndirect(blk_t* iarray,
                                                                blk_t dibindex) const {
  // Determine relative starting indices for indirect and direct blocks
  uint32_t indirect_start = dibindex == 0 ? ibindex_ : 0;
  uint32_t direct_start = (dibindex == 0 && indirect_start == ibindex_) ? bindex_ : 0;

  // Determine how many direct blocks we have already op'd within doubly indirect
  // context
  blk_t offset = 0;
  if (dibindex) {
    offset = kMinfsDirectPerIndirect * kMinfsDirectPerIndirect * dibindex -
             (ibindex_ * kMinfsDirectPerIndirect) + bindex_;
  }

  IndirectArgs params(op_,                                                                 // op
                      &iarray[indirect_start],                                             // array
                      fbl::min(count_ - offset, kMinfsDirectPerDindirect - direct_start),  // count
                      rel_bno_ + offset,                            // rel_bno
                      bnos_ == nullptr ? nullptr : &bnos_[offset],  // bnos
                      direct_start,                                 // bindex
                      ib_vmo_offset_ + dibindex + ibindex_          // ib_vmo_offset
  );
  return params;
}

}  // namespace minfs
