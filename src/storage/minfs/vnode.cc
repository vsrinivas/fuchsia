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
#include <zircon/time.h>

#include <cstdint>
#include <memory>
#include <string_view>

#include <fbl/algorithm.h>
#include <safemath/checked_math.h>

#include "src/lib/storage/vfs/cpp/trace.h"
#include "src/lib/storage/vfs/cpp/vfs_types.h"

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

void VnodeMinfs::HandleFsSpecificMessage(fidl::IncomingMessage& msg,
                                         fidl::Transaction* transaction) {
  fidl::WireDispatch<fuchsia_minfs::Minfs>(this, std::move(msg), transaction);
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
zx::status<> VnodeMinfs::BlocksShrink(PendingWork* transaction, blk_t start) {
  VnodeMapper mapper(this);
  VnodeIterator iterator;
  if (auto status = iterator.Init(&mapper, transaction, start); status.is_error())
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
      if (auto status = iterator.SetBlk(0); status.is_error())
        return status;
    }
    if (auto status = iterator.Advance(count); status.is_error())
      return status;
    block_count -= count;
  }
  if (auto status = iterator.Flush(); status.is_error())
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
  return zx::ok();
}

zx::status<LazyBuffer*> VnodeMinfs::GetIndirectFile() {
  if (!indirect_file_) {
    zx::status<std::unique_ptr<LazyBuffer>> buffer = LazyBuffer::Create(
        fs_->bc_.get(), "minfs-indirect-file", static_cast<uint32_t>(fs_->BlockSize()));
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
zx::status<> VnodeMinfs::InitVmo() {
  TRACE_DURATION("minfs", "VnodeMinfs::InitVmo");
  if (vmo_.is_valid()) {
    return zx::ok();
  }

  fs::Ticker ticker(fs_->StartTicker());
  const size_t vmo_size = fbl::round_up(GetSize(), fs_->BlockSize());
  if (zx_status_t status = zx::vmo::create(vmo_size, ZX_VMO_RESIZABLE, &vmo_); status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to initialize vmo; error: " << status;
    return zx::error(status);
  }
  vmo_size_ = vmo_size;

  zx_object_set_property(vmo_.get(), ZX_PROP_NAME, "minfs-inode", 11);

  if (zx_status_t status = fs_->bc_->BlockAttachVmo(vmo_, &vmoid_); status != ZX_OK) {
    vmo_.reset();
    return zx::error(status);
  }

  fs::BufferedOperationsBuilder builder;
  VnodeMapper mapper(this);
  VnodeIterator iterator;
  if (auto status = iterator.Init(&mapper, nullptr, 0); status.is_error())
    return status.take_error();
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
    if (auto status = iterator.Advance(count); status.is_error())
      return status.take_error();
    block_count -= count;
  }

  zx_status_t status = fs_->GetMutableBcache()->RunRequests(builder.TakeOperations());
  ValidateVmoTail(GetSize());
  // For now, we only track the time it takes to initialize VMOs.
  // TODO(fxbug.dev/51589): add more expansive init metrics.
  fs_->UpdateInitMetrics(0, 0, 0, 0, ticker.End());
  return zx::make_status(status);
}

#endif

void VnodeMinfs::AllocateIndirect(PendingWork* transaction, blk_t* block) {
  ZX_DEBUG_ASSERT(transaction != nullptr);
  fs_->BlockNew(transaction, block);
  inode_.block_count++;
}

zx::status<blk_t> VnodeMinfs::BlockGetWritable(Transaction* transaction, blk_t n) {
  VnodeMapper mapper(this);
  VnodeIterator iterator;
  if (auto status = iterator.Init(&mapper, transaction, n); status.is_error())
    return status.take_error();
  blk_t block = iterator.Blk();
  AcquireWritableBlock(transaction, n, block, &block);
  if (block != iterator.Blk()) {
    if (auto status = iterator.SetBlk(block); status.is_error())
      return status.take_error();
  }

  if (auto status = iterator.Flush(); status.is_error()) {
    return status.take_error();
  }

  return zx::ok(block);
}

zx::status<blk_t> VnodeMinfs::BlockGetReadable(blk_t n) {
  VnodeMapper mapper(this);
  zx::status<std::pair<blk_t, uint64_t>> mapping = mapper.MapToBlk(BlockRange(n, n + 1));
  if (mapping.is_error())
    return mapping.take_error();

  return zx::ok(mapping.value().first);
}

zx::status<> VnodeMinfs::ReadExactInternal(PendingWork* transaction, void* data, size_t len,
                                           size_t off) {
  size_t actual;
  auto status = ReadInternal(transaction, data, len, off, &actual);
  if (status.is_error()) {
    return status;
  }
  if (actual != len) {
    return zx::error(ZX_ERR_IO);
  }
  return zx::ok();
}

zx::status<> VnodeMinfs::WriteExactInternal(Transaction* transaction, const void* data, size_t len,
                                            size_t off) {
  size_t actual;
  auto status = WriteInternal(transaction, static_cast<const uint8_t*>(data), len, off, &actual);
  if (status.is_error()) {
    return status;
  }
  if (actual != len) {
    return zx::error(ZX_ERR_IO);
  }
  InodeSync(transaction, kMxFsSyncMtime);
  return zx::ok();
}

zx::status<> VnodeMinfs::RemoveInodeLink(Transaction* transaction) {
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
    // The open_count() needs to be read within the lock to make the compiler's checking happy,
    // but we don't actually need this lock and can run into recursive locking if we hold it for
    // the subsequent operations in this block.
    size_t oc;
    {
      std::lock_guard lock(mutex_);
      oc = open_count();
    }

    if (oc == 0) {
      // No need to flush/retain dirty cache or the reservations for unlinked
      // inode.
      DropCachedWrites();
      if (auto status = Purge(transaction); status.is_error()) {
        return status;
      }
    } else {
      fs_->AddUnlinked(transaction, this);
      if (IsDirectory()) {
        // If it's a directory, we need to remove the . and .. entries, which should be the only
        // entries.
        inode_.dirent_count = 0;
        if (auto status = TruncateInternal(transaction, 0); status.is_error()) {
          return status;
        }
      }
    }
  }

  InodeSync(transaction, kMxFsSyncMtime);
  return zx::ok();
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

void VnodeMinfs::RecycleNode() {
  {
    // Need to hold the lock to check open_count(), but be careful not to hold it across this class
    // getting deleted at the bottom of this function.
    std::lock_guard lock(mutex_);
    ZX_DEBUG_ASSERT(open_count() == 0);
  }
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
    fs_->bc_->GetDevice()->FifoTransaction(request, request_count);
  }
#endif
  if (indirect_file_) {
    auto status = indirect_file_->Detach(fs_->bc_.get());
    ZX_DEBUG_ASSERT(status.is_ok());
  }
}

zx::status<> VnodeMinfs::Purge(Transaction* transaction) {
  {
    std::lock_guard lock(mutex_);
    ZX_DEBUG_ASSERT(open_count() == 0);
  }
  ZX_DEBUG_ASSERT(IsUnlinked());
  fs_->VnodeRelease(this);
  return fs_->InoFree(transaction, this);
}

zx::status<> VnodeMinfs::RemoveUnlinked() {
  ZX_ASSERT(IsUnlinked());

  auto transaction_or = fs_->BeginTransaction(0, 0);
  if (transaction_or.is_error()) {
    // In case of error, we still need to release this vnode because it's not possible to retry,
    // and we cannot block destruction. The inode will get cleaned up on next remount.
    fs_->VnodeRelease(this);
    return transaction_or.take_error();
  }
  // The transaction may go async in journal layer. Hold the reference over this
  // vnode so that we keep the vnode around until the transaction is complete.
  transaction_or->PinVnode(fbl::RefPtr(this));

  fs_->RemoveUnlinked(transaction_or.value().get(), this);
  if (auto status = Purge(transaction_or.value().get()); status.is_error()) {
    return status;
  }

  fs_->CommitTransaction(std::move(transaction_or.value()));
  return zx::ok();
}

zx_status_t VnodeMinfs::CloseNode() {
  {
    std::lock_guard lock(mutex_);
    if (open_count() != 0) {
      return ZX_OK;
    }
  }

  if (!IsUnlinked()) {
    auto result = FlushCachedWrites();
    if (result.is_error()) {
      FX_LOGS(ERROR) << "Failed(" << result.error_value()
                     << ") to flush pending writes for inode:" << GetIno();
    }
    return result.status_value();
  }

  // This vnode is unlinked and open_count() == 0. We don't need not flush the dirty
  // contents of the vnode to disk.
  DropCachedWrites();
  return RemoveUnlinked().status_value();
}

// Internal read. Usable on directories.
zx::status<> VnodeMinfs::ReadInternal(PendingWork* transaction, void* vdata, size_t len, size_t off,
                                      size_t* actual) {
  // clip to EOF
  if (off >= GetSize()) {
    *actual = 0;
    return zx::ok();
  }
  if (len > (GetSize() - off)) {
    len = GetSize() - off;
  }

#ifdef __Fuchsia__
  if (auto status = InitVmo(); status.is_error()) {
    return status.take_error();
  } else if (zx_status_t status = vmo_.read(vdata, off, len); status != ZX_OK) {
    return zx::error(status);
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

    auto bno_or = BlockGetReadable(n);
    if (bno_or.is_error()) {
      return bno_or.take_error();
    }
    if (bno_or.value() != 0) {
      char bdata[fs_->BlockSize()];
      if (auto status = fs_->ReadDat(bno_or.value(), bdata); status.is_error()) {
        FX_LOGS(ERROR) << "Failed to read data block " << bno_or.value();
        return zx::error(ZX_ERR_IO);
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
  return zx::ok();
}

// Internal write. Usable on directories.
zx::status<> VnodeMinfs::WriteInternal(Transaction* transaction, const uint8_t* data, size_t len,
                                       size_t off, size_t* actual) {
  // We should be called after validating offset and length. Assert if they are invalid.
  auto new_size_or = safemath::CheckAdd(len, off);
  ZX_ASSERT(new_size_or.IsValid() && new_size_or.ValueOrDie() <= kMinfsMaxFileSize);

  if (len == 0) {
    *actual = 0;
    return zx::ok();
  }
#ifdef __Fuchsia__
  // TODO(planders): Once we are splitting up write transactions, assert this on host as well.
  ZX_DEBUG_ASSERT(len <= TransactionLimits::kMaxWriteBytes);
  if (auto status = InitVmo(); status.is_error()) {
    return status.take_error();
  }

#else
  size_t max_size = off + len;
#endif

  const uint8_t* const start = data;
  uint32_t n = static_cast<uint32_t>(off / fs_->BlockSize());
  size_t adjust = off % fs_->BlockSize();

  while (len > 0) {
    ZX_ASSERT(n < kMinfsMaxFileBlock);
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
      if (zx_status_t status = vmo_.set_size(new_size); status != ZX_OK) {
        break;
      }
      vmo_size_ = new_size;
    }

    // Update this block of the in-memory VMO
    if (zx_status_t status = vmo_.write(data, xfer_off, xfer); status != ZX_OK) {
      break;
    }

    if (!DirtyCacheEnabled()) {
      // Update this block on-disk
      auto bno_or = BlockGetWritable(transaction, n);
      if (bno_or.is_error()) {
        break;
      }

      IssueWriteback(transaction, n, bno_or.value() + fs_->Info().dat_block, 1);
    }
#else   // __Fuchsia__
    auto bno_or = BlockGetWritable(transaction, n);
    if (bno_or.is_error()) {
      break;
    }
    ZX_DEBUG_ASSERT(bno_or.value() != 0);
    char wdata[fs_->BlockSize()];
    if (auto status = fs_->bc_->Readblk(bno_or.value() + fs_->Info().dat_block, wdata);
        status.is_error()) {
      break;
    }
    memcpy(wdata + adjust, data, xfer);
    if (len < fs_->BlockSize() && max_size >= GetSize()) {
      memset(wdata + adjust + xfer, 0, fs_->BlockSize() - (adjust + xfer));
    }
    if (auto status = fs_->bc_->Writeblk(bno_or.value() + fs_->Info().dat_block, wdata);
        status.is_error()) {
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
      return zx::error(ZX_ERR_FILE_BIG);
    }

    FX_LOGS_FIRST_N(WARNING, 10) << "Minfs::WriteInternal can't write any bytes.";
    return zx::error(ZX_ERR_NO_SPACE);
  }

  if ((off + len) > GetSize()) {
    SetSize(static_cast<uint32_t>(off + len));
  }

  *actual = len;

  ValidateVmoTail(GetSize());
  return zx::ok();
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
    auto transaction_or = fs_->BeginTransaction(0, 0);
    if (transaction_or.is_error()) {
      return transaction_or.status_value();
    }
    InodeSync(transaction_or.value().get(), kMxFsSyncDefault);
    transaction_or->PinVnode(fbl::RefPtr(this));
    fs_->CommitTransaction(std::move(transaction_or.value()));
  }
  return ZX_OK;
}

VnodeMinfs::VnodeMinfs(Minfs* fs) : Vnode(fs), fs_(fs) {}

#ifdef __Fuchsia__
void VnodeMinfs::Notify(std::string_view name, fuchsia_io::wire::WatchEvent event) {
  watcher_.Notify(name, event);
}
zx_status_t VnodeMinfs::WatchDir(fs::Vfs* vfs, fuchsia_io::wire::WatchMask mask, uint32_t options,
                                 fidl::ServerEnd<fuchsia_io::DirectoryWatcher> watcher) {
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
  (*out)->SetSize(static_cast<uint32_t>((*out)->inode_.size));
}

#ifdef __Fuchsia__

zx::status<std::string> VnodeMinfs::GetDevicePath() const {
  return fs_->bc_->device()->GetDevicePath();
}

void VnodeMinfs::GetMetrics(GetMetricsRequestView request, GetMetricsCompleter::Sync& completer) {
  fuchsia_minfs::wire::Metrics metrics;
  zx_status_t status = fs_->GetMetrics(&metrics);
  completer.Reply(status,
                  status == ZX_OK
                      ? fidl::ObjectView<fuchsia_minfs::wire::Metrics>::FromExternal(&metrics)
                      : nullptr);
}

void VnodeMinfs::ToggleMetrics(ToggleMetricsRequestView request,
                               ToggleMetricsCompleter::Sync& completer) {
  fs_->SetMetrics(request->enable);
  completer.Reply(ZX_OK);
}

void VnodeMinfs::GetAllocatedRegions(GetAllocatedRegionsRequestView request,
                                     GetAllocatedRegionsCompleter::Sync& completer) {
  static_assert(sizeof(fuchsia_minfs::wire::BlockRegion) == sizeof(BlockRegion));
  static_assert(offsetof(fuchsia_minfs::wire::BlockRegion, offset) ==
                offsetof(BlockRegion, offset));
  static_assert(offsetof(fuchsia_minfs::wire::BlockRegion, length) ==
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

void VnodeMinfs::GetMountState(GetMountStateRequestView request,
                               GetMountStateCompleter::Sync& completer) {
  MountState state = fs_->GetMountState();
  completer.Reply(ZX_OK, fidl::ObjectView<MountState>::FromExternal(&state));
}

#endif

zx::status<> VnodeMinfs::TruncateInternal(Transaction* transaction, size_t len) {
  // We should be called after validating length. Assert if len is unexpected.
  ZX_ASSERT(len <= kMinfsMaxFileSize);

#ifdef __Fuchsia__
  // TODO(smklein): We should only init up to 'len'; no need
  // to read in the portion of a large file we plan on deleting.
  if (auto status = InitVmo(); status.is_error()) {
    FX_LOGS(ERROR) << "Truncate failed to initialize VMO: " << status.status_value();
    return zx::error(ZX_ERR_IO);
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

    if (auto shrink_or = BlocksShrink(transaction, start_bno); shrink_or.is_error()) {
      return shrink_or.take_error();
    }

#ifdef __Fuchsia__
    uint64_t decommit_offset = fbl::round_up(len, fs_->BlockSize());
    uint64_t decommit_length = fbl::round_up(inode_size, fs_->BlockSize()) - decommit_offset;
    if (decommit_length > 0) {
      zx_status_t status =
          vmo_.op_range(ZX_VMO_OP_DECOMMIT, decommit_offset, decommit_length, nullptr, 0);
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
      SetSize(static_cast<uint32_t>(start_bno * fs_->BlockSize()));
    }

    // Write zeroes to the rest of the remaining block, if it exists
    if (len < GetSize()) {
      char bdata[fs_->BlockSize()];
      blk_t rel_bno = static_cast<blk_t>(len / fs_->BlockSize());

      if (auto bno_or = BlockGetReadable(rel_bno); bno_or.is_ok()) {
        bno = bno_or.value();
      } else {
        FX_LOGS(ERROR) << "Truncate failed to get block " << rel_bno
                       << " of file: " << bno_or.status_value();
        return zx::error(ZX_ERR_IO);
      }

      size_t adjust = len % fs_->BlockSize();
#ifdef __Fuchsia__
      bool allocated = (bno != 0);
      if (allocated || HasPendingAllocation(rel_bno)) {
        if (zx_status_t status = vmo_.read(bdata, len - adjust, adjust); status != ZX_OK) {
          FX_LOGS(ERROR) << "Truncate failed to read last block: " << status;
          return zx::error(ZX_ERR_IO);
        }
        memset(bdata + adjust, 0, fs_->BlockSize() - adjust);

        if (zx_status_t status = vmo_.write(bdata, len - adjust, fs_->BlockSize());
            status != ZX_OK) {
          FX_LOGS(ERROR) << "Truncate failed to write last block: " << status;
          return zx::error(ZX_ERR_IO);
        }

        if (auto bno_or = BlockGetWritable(transaction, rel_bno); bno_or.is_ok()) {
          bno = bno_or.value();
        } else {
          FX_LOGS(ERROR) << "Truncate failed to get block " << rel_bno
                         << " of file: " << bno_or.status_value();
          return zx::error(ZX_ERR_IO);
        }
        IssueWriteback(transaction, rel_bno, bno + fs_->Info().dat_block, 1);
      }
#else   // __Fuchsia__
      if (bno != 0) {
        if (fs_->bc_->Readblk(bno + fs_->Info().dat_block, bdata).is_error()) {
          return zx::error(ZX_ERR_IO);
        }
        memset(bdata + adjust, 0, fs_->BlockSize() - adjust);
        if (fs_->bc_->Writeblk(bno + fs_->Info().dat_block, bdata).is_error()) {
          return zx::error(ZX_ERR_IO);
        }
      }
#endif  // __Fuchsia__
    }
  } else if (len > inode_size) {
    // Truncate should make the file longer, filled with zeroes.
    if (kMinfsMaxFileSize < len) {
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
#ifdef __Fuchsia__
    uint64_t new_size = fbl::round_up(len, fs_->BlockSize());
    if (zx_status_t status = vmo_.set_size(new_size); status != ZX_OK) {
      return zx::error(status);
    }
    vmo_size_ = new_size;
#endif
  } else {
    return zx::ok();
  }

  // Setting the size does not ensure the on-disk inode is updated. Ensuring
  // writeback occurs is the responsibility of the caller.
  SetSize(static_cast<uint32_t>(len));
  ValidateVmoTail(GetSize());
  return zx::ok();
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
    status = vn->fs_->bc_->Sync().status_value();
    cb(status);
  });
  return;
}

#endif

}  // namespace minfs
