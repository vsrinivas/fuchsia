// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/syslog/cpp/macros.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <zircon/device/vfs.h>
#include <zircon/time.h>

#include <cstdint>
#include <memory>

#include <fbl/algorithm.h>
#include <fbl/string_piece.h>
#include <fs/trace.h>
#include <fs/vfs_types.h>
#include <safemath/checked_math.h>

#ifdef __Fuchsia__
#include <lib/fidl-utils/bind.h>
#include <zircon/syscalls.h>

#include <utility>

#include <fbl/auto_lock.h>
#endif

#include "src/storage/minfs/directory.h"
#include "src/storage/minfs/file.h"
#include "src/storage/minfs/minfs_private.h"
#include "src/storage/minfs/unowned_vmo_buffer.h"
#include "src/storage/minfs/vnode.h"

namespace minfs {

#ifdef __Fuchsia__

void VnodeMinfs::HandleFsSpecificMessage(fidl_incoming_msg_t* msg, fidl::Transaction* transaction) {
  llcpp::fuchsia::minfs::Minfs::Dispatch(this, msg, transaction);
}

#endif  // __Fuchsia__

void VnodeMinfs::SetIno(ino_t ino) {
  ZX_DEBUG_ASSERT(ino_ == 0);
  ino_ = ino;
}

void VnodeMinfs::AddLink() {
  ZX_ASSERT_MSG(!add_overflow(inode_.link_count, 1, &inode_.link_count), "Exceeded max link count");
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
zx_status_t VnodeMinfs::BlocksShrink(PendingWork* transaction, blk_t start) {
  VnodeMapper mapper(this);
  VnodeIterator iterator;
  zx_status_t status = iterator.Init(&mapper, transaction, start);
  if (status != ZX_OK)
    return status;
  uint64_t block_count = VnodeMapper::kMaxBlocks - start;
  while (block_count > 0) {
    uint64_t count;
    if (iterator.Blk() == 0) {
      count = iterator.GetContiguousBlockCount(block_count);
    } else {
      count = 1;
      DeleteBlock(transaction, static_cast<blk_t>(iterator.file_block()), iterator.Blk(),
                  /*indirect=*/false);
      zx_status_t status = iterator.SetBlk(0);
      if (status != ZX_OK)
        return status;
    }
    status = iterator.Advance(count);
    if (status != ZX_OK)
      return status;
    block_count -= count;
  }
  status = iterator.Flush();
  if (status != ZX_OK)
    return status;
  // Shrink the buffer backing the virtual indirect file.
  if (indirect_file_) {
    uint64_t indirect_block_pointers;
    if (start <= VnodeMapper::kIndirectFileStartBlock) {
      indirect_block_pointers = 0;
    } else if (start <= VnodeMapper::kDoubleIndirectFileStartBlock) {
      indirect_block_pointers = start - VnodeMapper::kIndirectFileStartBlock;
    } else {
      indirect_block_pointers = (start - VnodeMapper::kDoubleIndirectFileStartBlock) +
                                (kMinfsIndirect + kMinfsDoublyIndirect) * kMinfsDirectPerIndirect;
    }
    indirect_file_->Shrink(
        fbl::round_up(indirect_block_pointers * sizeof(blk_t), fs_->BlockSize()) /
        fs_->BlockSize());
  }
  return ZX_OK;
}

zx::status<LazyBuffer*> VnodeMinfs::GetIndirectFile() {
  if (!indirect_file_) {
    zx::status<std::unique_ptr<LazyBuffer>> buffer =
        LazyBuffer::Create(fs_->bc_.get(), "minfs-indirect-file", fs_->BlockSize());
    if (buffer.is_error())
      return buffer.take_error();
    indirect_file_ = std::move(buffer).value();
  }
  return zx::ok(indirect_file_.get());
}

#ifdef __Fuchsia__

// TODO(smklein): Even this hack can be optimized; a bitmap could be used to
// track all 'empty/read/dirty' blocks for each vnode, rather than reading
// the entire file.
//
// TODO(fxbug.dev/51589): Add init metrics.
zx_status_t VnodeMinfs::InitVmo() {
  if (vmo_.is_valid()) {
    return ZX_OK;
  }

  zx_status_t status;
  const size_t vmo_size = fbl::round_up(GetSize(), fs_->BlockSize());
  if ((status = zx::vmo::create(vmo_size, ZX_VMO_RESIZABLE, &vmo_)) != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to initialize vmo; error: " << status;
    return status;
  }
  vmo_size_ = vmo_size;

  zx_object_set_property(vmo_.get(), ZX_PROP_NAME, "minfs-inode", 11);

  if ((status = fs_->bc_->device()->BlockAttachVmo(vmo_, &vmoid_)) != ZX_OK) {
    vmo_.reset();
    return status;
  }

  fs::BufferedOperationsBuilder builder;
  VnodeMapper mapper(this);
  VnodeIterator iterator;
  status = iterator.Init(&mapper, nullptr, 0);
  if (status != ZX_OK)
    return status;
  uint64_t block_count = vmo_size / fs_->BlockSize();
  while (block_count > 0) {
    blk_t block = iterator.Blk();
    uint64_t count = iterator.GetContiguousBlockCount(block_count);
    if (block) {
      fs_->ValidateBno(block);
      fs::internal::BorrowedBuffer buffer(vmoid_.get());
      builder.Add(storage::Operation{.type = storage::OperationType::kRead,
                                     .vmo_offset = iterator.file_block(),
                                     .dev_offset = block + fs_->Info().dat_block,
                                     .length = count},
                  &buffer);
    }
    status = iterator.Advance(count);
    if (status != ZX_OK)
      return status;
    block_count -= count;
  }
  status = fs_->GetMutableBcache()->RunRequests(builder.TakeOperations());
  ValidateVmoTail(GetSize());
  return status;
}

#endif

void VnodeMinfs::AllocateIndirect(PendingWork* transaction, blk_t* block) {
  ZX_DEBUG_ASSERT(transaction != nullptr);
  fs_->BlockNew(transaction, block);
  inode_.block_count++;
}

zx_status_t VnodeMinfs::BlockGetWritable(Transaction* transaction, blk_t n, blk_t* bno) {
  VnodeMapper mapper(this);
  VnodeIterator iterator;
  zx_status_t status = iterator.Init(&mapper, transaction, n);
  if (status != ZX_OK)
    return status;
  blk_t block = iterator.Blk();
  AcquireWritableBlock(transaction, n, block, &block);
  if (block != iterator.Blk()) {
    status = iterator.SetBlk(block);
    if (status != ZX_OK)
      return status;
  }
  *bno = block;
  return iterator.Flush();
}

zx_status_t VnodeMinfs::BlockGetReadable(blk_t n, blk_t* bno) {
  VnodeMapper mapper(this);
  zx::status<std::pair<blk_t, uint64_t>> mapping = mapper.MapToBlk(BlockRange(n, n + 1));
  if (mapping.is_error())
    return mapping.status_value();
  *bno = mapping.value().first;
  return ZX_OK;
}

zx_status_t VnodeMinfs::ReadExactInternal(PendingWork* transaction, void* data, size_t len,
                                          size_t off) {
  size_t actual;
  zx_status_t status = ReadInternal(transaction, data, len, off, &actual);
  if (status != ZX_OK) {
    return status;
  }
  if (actual != len) {
    return ZX_ERR_IO;
  }
  return ZX_OK;
}

zx_status_t VnodeMinfs::WriteExactInternal(Transaction* transaction, const void* data, size_t len,
                                           size_t off) {
  size_t actual;
  zx_status_t status =
      WriteInternal(transaction, static_cast<const uint8_t*>(data), len, off, &actual);
  if (status != ZX_OK) {
    return status;
  }
  if (actual != len) {
    return ZX_ERR_IO;
  }
  InodeSync(transaction, kMxFsSyncMtime);
  return ZX_OK;
}

zx_status_t VnodeMinfs::RemoveInodeLink(Transaction* transaction) {
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
      // No need to flush/retain dirty cache or the reservations for unlinked
      // inode.
      DropCachedWrites();
      zx_status_t status = Purge(transaction);
      if (status != ZX_OK) {
        return status;
      }
    } else {
      fs_->AddUnlinked(transaction, this);
      if (IsDirectory()) {
        // If it's a directory, we need to remove the . and .. entries, which should be the only
        // entries.
        inode_.dirent_count = 0;
        zx_status_t status = TruncateInternal(transaction, 0);
        if (status != ZX_OK) {
          return status;
        }
      }
    }
  }

  InodeSync(transaction, kMxFsSyncMtime);
  return ZX_OK;
}

void VnodeMinfs::ValidateVmoTail(uint64_t inode_size) const {
#if defined(MINFS_PARANOID_MODE) && defined(__Fuchsia__)
  if (!vmo_.is_valid()) {
    return;
  }

  // Verify that everything not allocated to "inode_size" in the
  // last block is filled with zeroes.
  char buf[fs_->BlockSize()];
  const size_t vmo_size = fbl::round_up(inode_size, fs_->BlockSize());
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
  if (vmoid_.IsAttached()) {
    request[request_count].vmoid = vmoid_.TakeId();
    request[request_count].opcode = BLOCKIO_CLOSE_VMO;
    request_count++;
  }
  if (request_count) {
    fs_->bc_->GetDevice()->FifoTransaction(&request[0], request_count);
  }
#endif
  if (indirect_file_) {
    zx_status_t status = indirect_file_->Detach(fs_->bc_.get());
    ZX_DEBUG_ASSERT(status == ZX_OK);
  }
}

zx_status_t VnodeMinfs::Open([[maybe_unused]] ValidatedOptions options,
                             fbl::RefPtr<Vnode>* out_redirect) {
  fd_count_++;
  return ZX_OK;
}

zx_status_t VnodeMinfs::Purge(Transaction* transaction) {
  ZX_DEBUG_ASSERT(fd_count_ == 0);
  ZX_DEBUG_ASSERT(IsUnlinked());
  fs_->VnodeRelease(this);
  return fs_->InoFree(transaction, this);
}

zx_status_t VnodeMinfs::RemoveUnlinked() {
  ZX_ASSERT(IsUnlinked());
  zx_status_t status;
  std::unique_ptr<Transaction> transaction;

  if ((status = fs_->BeginTransaction(0, 0, &transaction)) != ZX_OK) {
    // In case of error, we still need to release this vnode because it's not possible to retry,
    // and we cannot block destruction. The inode will get cleaned up on next remount.
    fs_->VnodeRelease(this);
    return status;
  }
  // The transaction may go async in journal layer. Hold the reference over this
  // vnode so that we keep the vnode around until the transaction is complete.
  transaction->PinVnode(fbl::RefPtr(this));

  fs_->RemoveUnlinked(transaction.get(), this);
  if ((status = Purge(transaction.get())) != ZX_OK) {
    return status;
  }

  fs_->CommitTransaction(std::move(transaction));
  return ZX_OK;
}

zx_status_t VnodeMinfs::Close() {
  ZX_DEBUG_ASSERT_MSG(fd_count_ > 0, "Closing ino with no fds open");
  fd_count_--;

  if (fd_count_ != 0) {
    return ZX_OK;
  }

  if (!IsUnlinked()) {
    auto result = FlushCachedWrites();
    if (result.is_error()) {
      FX_LOGS(ERROR) << "Failed(" << result.error_value()
                     << ") to flush pending writes for inode:" << GetIno();
    }
    return result.status_value();
  }

  // This vnode is unlinked and fd_count_ == 0. We don't need not flush the dirty
  // contents of the vnode to disk.
  DropCachedWrites();
  return RemoveUnlinked();
}

// Internal read. Usable on directories.
zx_status_t VnodeMinfs::ReadInternal(PendingWork* transaction, void* vdata, size_t len, size_t off,
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
  if ((status = InitVmo()) != ZX_OK) {
    return status;
  } else if ((status = vmo_.read(vdata, off, len)) != ZX_OK) {
    return status;
  } else {
    *actual = len;
  }
#else
  uint8_t* data = static_cast<uint8_t*>(vdata);
  uint8_t* start = data;
  uint32_t n = static_cast<uint32_t>(off / fs_->BlockSize());
  size_t adjust = off % fs_->BlockSize();

  while ((len > 0) && (n < kMinfsMaxFileBlock)) {
    size_t xfer;
    if (len > (fs_->BlockSize() - adjust)) {
      xfer = fs_->BlockSize() - adjust;
    } else {
      xfer = len;
    }

    blk_t bno;
    if ((status = BlockGetReadable(n, &bno)) != ZX_OK) {
      return status;
    }
    if (bno != 0) {
      char bdata[fs_->BlockSize()];
      if (fs_->ReadDat(bno, bdata)) {
        FX_LOGS(ERROR) << "Failed to read data block " << bno;
        return ZX_ERR_IO;
      }
      memcpy(data, bdata + adjust, xfer);
    } else {
      // If the block is not allocated, just read zeros
      memset(data, 0, xfer);
    }

    adjust = 0;
    len -= xfer;
    data = data + xfer;
    n++;
  }
  *actual = data - start;
#endif
  return ZX_OK;
}

// Internal write. Usable on directories.
zx_status_t VnodeMinfs::WriteInternal(Transaction* transaction, const uint8_t* data, size_t len,
                                      size_t off, size_t* actual) {
  if (len == 0) {
    *actual = 0;
    return ZX_OK;
  }
  zx_status_t status;
#ifdef __Fuchsia__
  // TODO(planders): Once we are splitting up write transactions, assert this on host as well.
  ZX_DEBUG_ASSERT(len <= TransactionLimits::kMaxWriteBytes);
  if ((status = InitVmo()) != ZX_OK) {
    return status;
  }

#else
  size_t max_size = off + len;
#endif

  const uint8_t* const start = data;
  uint32_t n = static_cast<uint32_t>(off / fs_->BlockSize());
  size_t adjust = off % fs_->BlockSize();

  while ((len > 0) && (n < kMinfsMaxFileBlock)) {
    size_t xfer;
    if (len > (fs_->BlockSize() - adjust)) {
      xfer = fs_->BlockSize() - adjust;
    } else {
      xfer = len;
    }

#ifdef __Fuchsia__
    size_t xfer_off = n * fs_->BlockSize() + adjust;
    if ((xfer_off + xfer) > vmo_size_) {
      size_t new_size = fbl::round_up(xfer_off + xfer, fs_->BlockSize());
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

    if (!DirtyCacheEnabled()) {
      // Update this block on-disk
      blk_t bno;
      if ((status = BlockGetWritable(transaction, n, &bno))) {
        break;
      }

      IssueWriteback(transaction, n, bno + fs_->Info().dat_block, 1);
    }
#else   // __Fuchsia__
    blk_t bno;
    if ((status = BlockGetWritable(transaction, n, &bno))) {
      break;
    }
    ZX_DEBUG_ASSERT(bno != 0);
    char wdata[fs_->BlockSize()];
    if (fs_->bc_->Readblk(bno + fs_->Info().dat_block, wdata)) {
      break;
    }
    memcpy(wdata + adjust, data, xfer);
    if (len < fs_->BlockSize() && max_size >= GetSize()) {
      memset(wdata + adjust + xfer, 0, fs_->BlockSize() - (adjust + xfer));
    }
    if (fs_->bc_->Writeblk(bno + fs_->Info().dat_block, wdata)) {
      break;
    }
#endif  // __Fuchsia__

    adjust = 0;
    len -= xfer;
    data = data + xfer;
    n++;
  }

  len = data - start;
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
  FX_LOGS(DEBUG) << "minfs_getattr() vn=" << this << "(#" << ino_ << ")";
  // This transaction exists because acquiring the block size and block
  // count may be unsafe without locking.
  //
  // TODO(unknown): Improve locking semantics of pending data allocation to make this less
  // confusing.
  Transaction transaction(fs_);
  *a = fs::VnodeAttributes();
  a->mode = DTYPE_TO_VTYPE(MinfsMagicType(inode_.magic)) | V_IRUSR | V_IWUSR | V_IRGRP | V_IROTH;
  a->inode = ino_;
  a->content_size = GetSize();
  a->storage_size = GetBlockCount() * fs_->BlockSize();
  a->link_count = inode_.link_count;
  a->creation_time = inode_.create_time;
  a->modification_time = inode_.modify_time;
  return ZX_OK;
}

zx_status_t VnodeMinfs::SetAttributes(fs::VnodeAttributesUpdate attr) {
  int dirty = 0;
  FX_LOGS(DEBUG) << "minfs_setattr() vn=" << this << "(#" << ino_ << ")";
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

  // Commit transaction if dirty cache is disabled. Otherwise this will
  // happen later.
  if (dirty && !DirtyCacheEnabled()) {
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
  uint32_t reserved_blocks = Vfs()->BlocksReserved();
  Transaction transaction(fs_);
  *info = {};
  info->block_size = fs_->BlockSize();
  info->max_filename_size = kMinfsMaxNameSize;
  info->fs_type = VFS_TYPE_MINFS;
  info->fs_id = fs_->GetFsId();
  info->total_bytes = fs_->Info().block_count * fs_->Info().block_size;
  info->used_bytes = (fs_->Info().alloc_block_count + reserved_blocks) * fs_->Info().block_size;
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

void VnodeMinfs::GetMetrics(GetMetricsCompleter::Sync& completer) {
  ::llcpp::fuchsia::minfs::Metrics metrics;
  zx_status_t status = fs_->GetMetrics(&metrics);
  completer.Reply(status, status == ZX_OK ? fidl::unowned_ptr(&metrics) : nullptr);
}

void VnodeMinfs::ToggleMetrics(bool enable, ToggleMetricsCompleter::Sync& completer) {
  fs_->SetMetrics(enable);
  completer.Reply(ZX_OK);
}

void VnodeMinfs::GetAllocatedRegions(GetAllocatedRegionsCompleter::Sync& completer) {
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

void VnodeMinfs::GetMountState(GetMountStateCompleter::Sync& completer) {
  fidl::aligned<MountState> state = fs_->GetMountState();
  completer.Reply(ZX_OK, fidl::unowned_ptr(&state));
}

#endif

zx_status_t VnodeMinfs::TruncateInternal(Transaction* transaction, size_t len) {
  zx_status_t status = ZX_OK;
#ifdef __Fuchsia__
  // TODO(smklein): We should only init up to 'len'; no need
  // to read in the portion of a large file we plan on deleting.
  if ((status = InitVmo()) != ZX_OK) {
    FX_LOGS(ERROR) << "Truncate failed to initialize VMO: " << status;
    return ZX_ERR_IO;
  }
#endif

  uint64_t inode_size = GetSize();
  if (len < inode_size) {
    // Truncate should make the file shorter.
    blk_t bno = safemath::checked_cast<blk_t>(inode_size / fs_->BlockSize());

    // Truncate to the nearest block.
    blk_t trunc_bno = static_cast<blk_t>(len / fs_->BlockSize());
    // [start_bno, EOF) blocks should be deleted entirely.
    blk_t start_bno = static_cast<blk_t>((len % fs_->BlockSize() == 0) ? trunc_bno : trunc_bno + 1);

    if ((status = BlocksShrink(transaction, start_bno)) != ZX_OK) {
      return status;
    }

#ifdef __Fuchsia__
    uint64_t decommit_offset = fbl::round_up(len, fs_->BlockSize());
    uint64_t decommit_length = fbl::round_up(inode_size, fs_->BlockSize()) - decommit_offset;
    if (decommit_length > 0) {
      status = vmo_.op_range(ZX_VMO_OP_DECOMMIT, decommit_offset, decommit_length, nullptr, 0);
      if (status != ZX_OK) {
        // TODO(fxbug.dev/35948): This is a known issue; the additional logging here is to help
        // diagnose.
        FX_LOGS(ERROR) << "TruncateInternal: Modifying node length from " << inode_size << " to "
                       << len;
        FX_LOGS(ERROR) << "  Decommit from offset " << decommit_offset << ", length "
                       << decommit_length << ". Status: " << status;
        ZX_ASSERT(status == ZX_OK);
      }
    }
#endif
    // Shrink the size to be block-aligned if we are removing blocks from
    // the end of the vnode.
    if (start_bno * fs_->BlockSize() < inode_size) {
      SetSize(start_bno * fs_->BlockSize());
    }

    // Write zeroes to the rest of the remaining block, if it exists
    if (len < GetSize()) {
      char bdata[fs_->BlockSize()];
      blk_t rel_bno = static_cast<blk_t>(len / fs_->BlockSize());
      bno = 0;
      if ((status = BlockGetReadable(rel_bno, &bno)) != ZX_OK) {
        FX_LOGS(ERROR) << "Truncate failed to get block " << rel_bno << " of file: " << status;
        return ZX_ERR_IO;
      }

      size_t adjust = len % fs_->BlockSize();
#ifdef __Fuchsia__
      bool allocated = (bno != 0);
      if (allocated || HasPendingAllocation(rel_bno)) {
        if ((status = vmo_.read(bdata, len - adjust, adjust)) != ZX_OK) {
          FX_LOGS(ERROR) << "Truncate failed to read last block: " << status;
          return ZX_ERR_IO;
        }
        memset(bdata + adjust, 0, fs_->BlockSize() - adjust);

        if ((status = vmo_.write(bdata, len - adjust, fs_->BlockSize())) != ZX_OK) {
          FX_LOGS(ERROR) << "Truncate failed to write last block: " << status;
          return ZX_ERR_IO;
        }

        if ((status = BlockGetWritable(transaction, rel_bno, &bno)) != ZX_OK) {
          FX_LOGS(ERROR) << "Truncate failed to get block " << rel_bno << " of file: " << status;
          return ZX_ERR_IO;
        }
        IssueWriteback(transaction, rel_bno, bno + fs_->Info().dat_block, 1);
      }
#else   // __Fuchsia__
      if (bno != 0) {
        if (fs_->bc_->Readblk(bno + fs_->Info().dat_block, bdata)) {
          return ZX_ERR_IO;
        }
        memset(bdata + adjust, 0, fs_->BlockSize() - adjust);
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
    uint64_t new_size = fbl::round_up(len, fs_->BlockSize());
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
  // The transaction may go async in journal layer. Hold the reference over this
  // vnode so that we keep the vnode around until the transaction is complete.
  auto vn = fbl::RefPtr(this);
  fs_->Sync([vn, cb = std::move(closure)](zx_status_t status) mutable {
    // This is called on the journal thread. Operations here must be threadsafe.
    if (status != ZX_OK) {
      cb(status);
      return;
    }
    status = vn->fs_->bc_->Sync();
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
  SetRemote(std::move(h.client_end()));
  return ZX_OK;
}
#endif

}  // namespace minfs
