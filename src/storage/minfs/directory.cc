// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/directory.h"

#include <fcntl.h>
#include <lib/fit/defer.h>
#include <lib/syslog/cpp/macros.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <zircon/device/vfs.h>
#include <zircon/time.h>

#include <memory>
#include <string_view>
#include <utility>

#include <fbl/algorithm.h>

#include "lib/zx/status.h"
#include "src/lib/storage/vfs/cpp/debug.h"
#include "src/lib/storage/vfs/cpp/vfs_types.h"
#include "zircon/assert.h"
#include "zircon/errors.h"

#ifdef __Fuchsia__
#include <lib/fidl-utils/bind.h>
#include <zircon/syscalls.h>

#include <utility>

#include <fbl/auto_lock.h>
#endif

#include "src/storage/minfs/minfs_private.h"
#include "src/storage/minfs/unowned_vmo_buffer.h"
#include "src/storage/minfs/vnode.h"

namespace minfs {
namespace {

zx::status<> ValidateDirent(Dirent* de, size_t bytes_read, size_t off) {
  if (bytes_read < kMinfsDirentSize) {
    FX_LOGS(ERROR) << "vn_dir: Short read (" << bytes_read << " bytes) at offset " << off;
    return zx::error(ZX_ERR_IO);
  }
  uint32_t reclen = static_cast<uint32_t>(DirentReservedSize(de, off));
  if (reclen < kMinfsDirentSize) {
    FX_LOGS(ERROR) << "vn_dir: Could not read dirent at offset: " << off;
    return zx::error(ZX_ERR_IO);
  }
  if ((off + reclen > kMinfsMaxDirectorySize) || (reclen & kMinfsDirentAlignmentMask)) {
    FX_LOGS(ERROR) << "vn_dir: bad reclen " << reclen << " > " << kMinfsMaxDirectorySize;
    return zx::error(ZX_ERR_IO);
  }
  if (de->ino != 0) {
    if ((de->namelen == 0) || (de->namelen > (reclen - kMinfsDirentSize))) {
      FX_LOGS(ERROR) << "vn_dir: bad namelen " << de->namelen << " / " << reclen;
      return zx::error(ZX_ERR_IO);
    }
  }
  return zx::ok();
}

}  // namespace

Directory::Directory(Minfs* fs) : VnodeMinfs(fs) {}

Directory::~Directory() = default;

blk_t Directory::GetBlockCount() const { return GetInode()->block_count; }

uint64_t Directory::GetSize() const { return GetInode()->size; }

void Directory::SetSize(uint32_t new_size) { GetMutableInode()->size = new_size; }

void Directory::AcquireWritableBlock(Transaction* transaction, blk_t local_bno, blk_t old_bno,
                                     blk_t* out_bno) {
  bool using_new_block = (old_bno == 0);
  if (using_new_block) {
    Vfs()->BlockNew(transaction, out_bno);
    GetMutableInode()->block_count++;
  }
}

void Directory::DeleteBlock(PendingWork* transaction, blk_t local_bno, blk_t old_bno,
                            bool indirect) {
  // If we found a block that was previously allocated, delete it.
  if (old_bno != 0) {
    transaction->DeallocateBlock(old_bno);
    GetMutableInode()->block_count--;
  }
}

#ifdef __Fuchsia__
void Directory::IssueWriteback(Transaction* transaction, blk_t vmo_offset, blk_t dev_offset,
                               blk_t count) {
  storage::Operation operation = {
      .type = storage::OperationType::kWrite,
      .vmo_offset = vmo_offset,
      .dev_offset = dev_offset,
      .length = count,
  };
  UnownedVmoBuffer buffer(vmo());
  transaction->EnqueueMetadata(operation, &buffer);
}

bool Directory::HasPendingAllocation(blk_t vmo_offset) { return false; }

void Directory::CancelPendingWriteback() {}

#endif

zx::status<Directory::IteratorCommand> Directory::DirentCallbackFind(fbl::RefPtr<Directory> vndir,
                                                                     Dirent* de, DirArgs* args) {
  if ((de->ino != 0) && std::string_view(de->name, de->namelen) == args->name) {
    args->ino = de->ino;
    args->type = de->type;
    return zx::ok(IteratorCommand::kIteratorDone);
  }
  return NextDirent(de, &args->offs);
}

zx::status<> Directory::CanUnlink() const {
  // directories must be empty (dirent_count == 2)
  if (GetInode()->dirent_count != 2) {
    // if we have more than "." and "..", not empty, cannot unlink
    return zx::error(ZX_ERR_NOT_EMPTY);
#ifdef __Fuchsia__
  } else if (IsRemote()) {
    // we cannot unlink mount points
    return zx::error(ZX_ERR_UNAVAILABLE);
#endif
  }
  return zx::ok();
}

zx::status<Directory::IteratorCommand> Directory::UnlinkChild(Transaction* transaction,
                                                              fbl::RefPtr<VnodeMinfs> childvn,
                                                              Dirent* de, DirectoryOffset* offs) {
  // Coalesce the current dirent with the previous/next dirent, if they
  // (1) exist and (2) are free.
  size_t off_prev = offs->off_prev;
  size_t off = offs->off;
  size_t off_next = off + DirentReservedSize(de, off);

  // Read the direntries we're considering merging with.
  // Verify they are free and small enough to merge.
  size_t coalesced_size = DirentReservedSize(de, off);
  // Coalesce with "next" first, so the kMinfsReclenLast bit can easily flow
  // back to "de" and "de_prev".
  if (!(de->reclen & kMinfsReclenLast)) {
    Dirent de_next;
    size_t len = kMinfsDirentSize;
    if (auto status = ReadExactInternal(transaction, &de_next, len, off_next); status.is_error()) {
      FX_LOGS(ERROR) << "unlink: Failed to read next dirent";
      return status.take_error();
    }
    if (auto status = ValidateDirent(&de_next, len, off_next); status.is_error()) {
      FX_LOGS(ERROR) << "unlink: Read invalid dirent";
      return status.take_error();
    }
    if (de_next.ino == 0) {
      coalesced_size += DirentReservedSize(&de_next, off_next);
      // If the next entry *was* last, then 'de' is now last.
      de->reclen |= (de_next.reclen & kMinfsReclenLast);
    }
  }
  if (off_prev != off) {
    Dirent de_prev;
    size_t len = kMinfsDirentSize;
    if (auto status = ReadExactInternal(transaction, &de_prev, len, off_prev); status.is_error()) {
      FX_LOGS(ERROR) << "unlink: Failed to read previous dirent";
      return status.take_error();
    }
    if (auto status = ValidateDirent(&de_prev, len, off_prev); status.is_error()) {
      FX_LOGS(ERROR) << "unlink: Read invalid dirent";
      return status.take_error();
    }
    if (de_prev.ino == 0) {
      coalesced_size += DirentReservedSize(&de_prev, off_prev);
      off = off_prev;
    }
  }

  if (!(de->reclen & kMinfsReclenLast) && (coalesced_size >= kMinfsReclenMask)) {
    // Should only be possible if the on-disk record format is corrupted
    FX_LOGS(ERROR) << "unlink: Corrupted direntry with impossibly large size";
    return zx::error(ZX_ERR_IO);
  }
  de->ino = 0;
  de->reclen =
      static_cast<uint32_t>(coalesced_size & kMinfsReclenMask) | (de->reclen & kMinfsReclenLast);
  // Erase dirent (replace with 'empty' dirent)
  if (auto status = WriteExactInternal(transaction, de, kMinfsDirentSize, off); status.is_error()) {
    return status.take_error();
  }

  if (de->reclen & kMinfsReclenLast) {
    // Truncating the directory merely removed unused space; if it fails,
    // the directory contents are still valid.
    [[maybe_unused]] auto _ = TruncateInternal(transaction, off + kMinfsDirentSize);
  }

  GetMutableInode()->dirent_count--;

  if (MinfsMagicType(childvn->GetInode()->magic) == kMinfsTypeDir) {
    // Child directory had '..' which pointed to parent directory
    GetMutableInode()->link_count--;
  }

  if (auto status = childvn->RemoveInodeLink(transaction); status.is_error()) {
    return status.take_error();
  }
  transaction->PinVnode(fbl::RefPtr(this));
  transaction->PinVnode(std::move(childvn));
  return zx::ok(IteratorCommand::kIteratorSaveSync);
}

// caller is expected to prevent unlink of "." or ".."
zx::status<Directory::IteratorCommand> Directory::DirentCallbackUnlink(fbl::RefPtr<Directory> vndir,
                                                                       Dirent* de, DirArgs* args) {
  if ((de->ino == 0) || std::string_view(de->name, de->namelen) != args->name) {
    return NextDirent(de, &args->offs);
  }

  auto vn_or = vndir->Vfs()->VnodeGet(de->ino);
  if (vn_or.is_error()) {
    return vn_or.take_error();
  }

  // If a directory was requested, then only try unlinking a directory
  if ((args->type == kMinfsTypeDir) && !vn_or->IsDirectory()) {
    return zx::error(ZX_ERR_NOT_DIR);
  }
  if (auto status = vn_or->CanUnlink(); status.is_error()) {
    return status.take_error();
  }
  return vndir->UnlinkChild(args->transaction, std::move(vn_or.value()), de, &args->offs);
}

// same as unlink, but do not validate vnode
zx::status<Directory::IteratorCommand> Directory::DirentCallbackForceUnlink(
    fbl::RefPtr<Directory> vndir, Dirent* de, DirArgs* args) {
  if ((de->ino == 0) || std::string_view(de->name, de->namelen) != args->name) {
    return NextDirent(de, &args->offs);
  }

  auto vn_or = vndir->Vfs()->VnodeGet(de->ino);
  if (vn_or.is_error()) {
    return vn_or.take_error();
  }
  return vndir->UnlinkChild(args->transaction, std::move(vn_or.value()), de, &args->offs);
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
zx::status<Directory::IteratorCommand> Directory::DirentCallbackAttemptRename(
    fbl::RefPtr<Directory> vndir, Dirent* de, DirArgs* args) {
  if ((de->ino == 0) || std::string_view(de->name, de->namelen) != args->name) {
    return NextDirent(de, &args->offs);
  }

  auto vn_or = vndir->Vfs()->VnodeGet(de->ino);
  if (vn_or.is_error()) {
    return vn_or.take_error();
  }
  if (args->ino == vn_or->GetIno()) {
    // cannot rename node to itself
    return zx::error(ZX_ERR_BAD_STATE);
  }
  if (args->type != de->type) {
    // cannot rename directory to file (or vice versa)
    return args->type == kMinfsTypeDir ? zx::error(ZX_ERR_NOT_DIR) : zx::error(ZX_ERR_NOT_FILE);
  }
  if (auto status = vn_or->CanUnlink(); status.is_error()) {
    // if we cannot unlink the target, we cannot rename the target
    return status.take_error();
  }

  // If we are renaming ON TOP of a directory, then we can skip
  // updating the parent link count -- the old directory had a ".." entry to
  // the parent (link count of 1), but the new directory will ALSO have a ".."
  // entry, making the rename operation idempotent w.r.t. the parent link
  // count.

  if (auto status = vn_or->RemoveInodeLink(args->transaction); status.is_error()) {
    return status.take_error();
  }

  de->ino = args->ino;

  if (auto status =
          vndir->WriteExactInternal(args->transaction, de, DirentSize(de->namelen), args->offs.off);
      status.is_error()) {
    return status.take_error();
  }

  args->transaction->PinVnode(std::move(vn_or.value()));
  args->transaction->PinVnode(vndir);
  return zx::ok(IteratorCommand::kIteratorSaveSync);
}

zx::status<Directory::IteratorCommand> Directory::DirentCallbackUpdateInode(
    fbl::RefPtr<Directory> vndir, Dirent* de, DirArgs* args) {
  if ((de->ino == 0) || std::string_view(de->name, de->namelen) != args->name) {
    return NextDirent(de, &args->offs);
  }

  de->ino = args->ino;

  if (auto status =
          vndir->WriteExactInternal(args->transaction, de, DirentSize(de->namelen), args->offs.off);
      status.is_error()) {
    return status.take_error();
    ;
  }
  args->transaction->PinVnode(vndir);
  return zx::ok(IteratorCommand::kIteratorSaveSync);
}

zx::status<Directory::IteratorCommand> Directory::DirentCallbackFindSpace(
    fbl::RefPtr<Directory> vndir, Dirent* de, DirArgs* args) {
  // Reserved space for this record (possibly going to the max directory size if it's the last
  // one).
  uint32_t reserved_size = static_cast<uint32_t>(DirentReservedSize(de, args->offs.off));
  if (de->ino == 0) {
    // Empty entry, do we fit?
    if (args->reclen > reserved_size) {
      return NextDirent(de, &args->offs);  // Don't fit.
    }
    return zx::ok(IteratorCommand::kIteratorDone);
  }

  // Filled entry, can we sub-divide? The entry might not use the full amount of space reserved for
  // it if a larger entry was later filled with a smaller one. We might be able to fit in the
  // extra.
  uint32_t used_size = static_cast<uint32_t>(DirentSize(de->namelen));
  if (used_size > reserved_size) {
    FX_LOGS(ERROR) << "bad reclen (smaller than dirent) " << reserved_size << " < " << used_size;
    return zx::error(ZX_ERR_IO);
  }
  uint32_t available_size = reserved_size - used_size;
  if (available_size < args->reclen) {
    return NextDirent(de, &args->offs);  // Don't fit in the extra space.
  }

  // Could subdivide this one.
  return zx::ok(IteratorCommand::kIteratorDone);
}

// Updates offset information to move to the next direntry in the directory.
zx::status<Directory::IteratorCommand> Directory::NextDirent(Dirent* de, DirectoryOffset* offs) {
  offs->off_prev = offs->off;
  offs->off += DirentReservedSize(de, offs->off);
  return zx::ok(IteratorCommand::kIteratorNext);
}

zx::status<> Directory::AppendDirent(DirArgs* args) {
  DirentBuffer dirent_buffer;
  Dirent* de = &dirent_buffer.dirent;

  size_t r;
  if (auto status = ReadInternal(args->transaction, de, kMinfsMaxDirentSize, args->offs.off, &r);
      status.is_error()) {
    return status;
  }

  if (auto status = ValidateDirent(de, r, args->offs.off); status.is_error()) {
    return status;
  }

  uint32_t reclen = static_cast<uint32_t>(DirentReservedSize(de, args->offs.off));
  if (de->ino == 0) {
    // empty entry, do we fit?
    if (args->reclen > reclen) {
      FX_LOGS(ERROR) << "Directory::AppendDirent: new entry can't fit in requested empty dirent.";
      return zx::error(ZX_ERR_NO_SPACE);
    }
  } else {
    // filled entry, can we sub-divide?
    uint32_t size = static_cast<uint32_t>(DirentSize(de->namelen));
    if (size > reclen) {
      FX_LOGS(ERROR) << "bad reclen (smaller than dirent) " << reclen << " < " << size;
      return zx::error(ZX_ERR_IO);
    }
    uint32_t extra = reclen - size;
    if (extra < args->reclen) {
      FX_LOGS(ERROR) << "Directory::AppendDirent: new entry can't fit in free space.";
      return zx::error(ZX_ERR_NO_SPACE);
    }
    // shrink existing entry
    bool was_last_record = de->reclen & kMinfsReclenLast;
    de->reclen = size;
    if (auto status =
            WriteExactInternal(args->transaction, de, DirentSize(de->namelen), args->offs.off);
        status.is_error()) {
      return status;
    }

    args->offs.off += size;
    // Overwrite dirent data to reflect the new dirent.
    de->reclen = extra | (was_last_record ? kMinfsReclenLast : 0);
  }

  de->ino = args->ino;
  de->type = static_cast<uint8_t>(args->type);
  de->namelen = static_cast<uint8_t>(args->name.length());
  memcpy(de->name, args->name.data(), de->namelen);
  if (auto status =
          WriteExactInternal(args->transaction, de, DirentSize(de->namelen), args->offs.off);
      status.is_error()) {
    return status;
  }

  if (args->type == kMinfsTypeDir) {
    // Child directory has '..' which will point to parent directory
    GetMutableInode()->link_count++;
  }

  GetMutableInode()->dirent_count++;
  GetMutableInode()->seq_num++;
  InodeSync(args->transaction, kMxFsSyncMtime);
  args->transaction->PinVnode(fbl::RefPtr(this));
  return zx::ok();
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
zx::status<bool> Directory::ForEachDirent(DirArgs* args, const DirentCallback func) {
  DirentBuffer dirent_buffer;
  Dirent* de = &dirent_buffer.dirent;

  args->offs.off = 0;
  args->offs.off_prev = 0;
  while (args->offs.off + kMinfsDirentSize < kMinfsMaxDirectorySize && args->offs.off < GetSize()) {
    FX_LOGS(DEBUG) << "Reading dirent at offset " << args->offs.off;
    size_t r;

    if (auto status = ReadInternal(args->transaction, de, kMinfsMaxDirentSize, args->offs.off, &r);
        status.is_error()) {
      return status.take_error();
    }
    if (auto status = ValidateDirent(de, r, args->offs.off); status.is_error()) {
      return status.take_error();
    }

    auto command_or = func(fbl::RefPtr<Directory>(this), de, args);
    if (command_or.is_error()) {
      return command_or.take_error();
    }

    switch (command_or.value()) {
      case IteratorCommand::kIteratorNext:
        break;
      case IteratorCommand::kIteratorSaveSync:
        GetMutableInode()->seq_num++;
        InodeSync(args->transaction, kMxFsSyncMtime);
        args->transaction->PinVnode(fbl::RefPtr(this));
        return zx::ok(true);
      case IteratorCommand::kIteratorDone:
        return zx::ok(true);
    }
  }

  return zx::ok(false);
}

fs::VnodeProtocolSet Directory::GetProtocols() const { return fs::VnodeProtocol::kDirectory; }

zx_status_t Directory::Read(void* data, size_t len, size_t off, size_t* out_actual) {
  return ZX_ERR_NOT_FILE;
}

zx_status_t Directory::Write(const void* data, size_t len, size_t offset, size_t* out_actual) {
  return ZX_ERR_NOT_FILE;
}

zx_status_t Directory::Append(const void* data, size_t len, size_t* out_end, size_t* out_actual) {
  return ZX_ERR_NOT_FILE;
}

zx_status_t Directory::Lookup(std::string_view name, fbl::RefPtr<fs::Vnode>* out) {
  TRACE_DURATION("minfs", "Directory::Lookup", "name", name);
  ZX_DEBUG_ASSERT(fs::IsValidName(name));

  auto vn_or = LookupInternal(name);
  if (vn_or.is_ok()) {
    *out = std::move(vn_or.value());
  }

  return vn_or.status_value();
}

zx::status<fbl::RefPtr<fs::Vnode>> Directory::LookupInternal(std::string_view name) {
  DirArgs args;
  args.name = name;

  bool success = false;
  fs::Ticker ticker(Vfs()->StartTicker());
  auto get_metrics = fit::defer(
      [&ticker, &success, this]() { Vfs()->UpdateLookupMetrics(success, ticker.End()); });

  zx::status<bool> found_or = ForEachDirent(&args, DirentCallbackFind);
  if (found_or.is_error()) {
    return found_or.take_error();
  }
  if (!found_or.value()) {
    return zx::error(ZX_ERR_NOT_FOUND);
  }

  auto vn_or = Vfs()->VnodeGet(args.ino);
  if (vn_or.is_error()) {
    return vn_or.take_error();
  }

  return zx::ok(std::move(vn_or.value()));
}

struct DirCookie {
  size_t off;         // Offset into directory
  uint32_t reserved;  // Unused
  uint32_t seqno;     // inode seq no
};

static_assert(sizeof(DirCookie) <= sizeof(fs::VdirCookie),
              "MinFS DirCookie too large to fit in IO state");

zx_status_t Directory::Readdir(fs::VdirCookie* cookie, void* dirents, size_t len,
                               size_t* out_actual) {
  TRACE_DURATION("minfs", "Directory::Readdir");
  FX_LOGS(DEBUG) << "minfs_readdir() vn=" << this << "(#" << GetIno() << ") cookie=" << cookie
                 << " len=" << len;

  if (IsUnlinked()) {
    *out_actual = 0;
    return ZX_OK;
  }

  DirCookie* dc = reinterpret_cast<DirCookie*>(cookie);
  fs::DirentFiller df(dirents, len);

  size_t off = dc->off;
  size_t r;

  DirentBuffer dirent_buffer;
  Dirent* de = &dirent_buffer.dirent;

  if (off != 0 && dc->seqno != GetInode()->seq_num) {
    // The offset *might* be invalid, if we called Readdir after a directory
    // has been modified. In this case, we need to re-read the directory
    // until we get to the direntry at or after the previously identified offset.

    size_t off_recovered = 0;
    while (off_recovered < off) {
      if (off_recovered + kMinfsDirentSize >= kMinfsMaxDirectorySize) {
        FX_LOGS(ERROR) << "Readdir: Corrupt dirent; dirent reclen too large";
        goto fail;
      }
      auto read_status = ReadInternal(nullptr, de, kMinfsMaxDirentSize, off_recovered, &r);
      if (read_status.is_error() || ValidateDirent(de, r, off_recovered).is_error()) {
        FX_LOGS(ERROR) << "Readdir: Corrupt dirent unreadable/failed validation";
        goto fail;
      }
      off_recovered += DirentReservedSize(de, off_recovered);
    }
    off = off_recovered;
  }

  while (off + kMinfsDirentSize < kMinfsMaxDirectorySize) {
    if (auto status = ReadInternal(nullptr, de, kMinfsMaxDirentSize, off, &r); status.is_error()) {
      FX_LOGS(ERROR) << "Readdir: Unreadable dirent " << status.status_value();
      goto fail;
    }
    if (auto status = ValidateDirent(de, r, off); status.is_error()) {
      FX_LOGS(ERROR) << "Readdir: Corrupt dirent failed validation " << status.status_value();
      goto fail;
    }

    std::string_view name(de->name, de->namelen);

    if (de->ino && name != "..") {
      zx_status_t status;
      if ((status = df.Next(name, de->type, de->ino)) != ZX_OK) {
        // no more space
        goto done;
      }
    }

    off += DirentReservedSize(de, off);
  }

done:
  // save our place in the DirCookie
  dc->off = off;
  dc->seqno = GetInode()->seq_num;
  *out_actual = df.BytesFilled();
  ZX_DEBUG_ASSERT(*out_actual <= len);  // Otherwise, we're overflowing the input buffer.
  return ZX_OK;

fail:
  dc->off = 0;
  return ZX_ERR_IO;
}

zx_status_t Directory::Create(std::string_view name, uint32_t mode, fbl::RefPtr<fs::Vnode>* out) {
  TRACE_DURATION("minfs", "Directory::Create", "name", name);

  if (!fs::IsValidName(name)) {
    return ZX_ERR_INVALID_ARGS;
  }

  bool success = false;
  fs::Ticker ticker(Vfs()->StartTicker());
  auto get_metrics = fit::defer(
      [&ticker, &success, this]() { Vfs()->UpdateCreateMetrics(success, ticker.End()); });

  if (IsUnlinked()) {
    return ZX_ERR_BAD_STATE;
  }

  DirArgs args;
  args.name = name;

  // Ensure file does not exist.
  {
    TRACE_DURATION("minfs", "Directory::Create::ExistenceCheck");
    zx::status<bool> found_or = ForEachDirent(&args, DirentCallbackFind);
    if (found_or.is_error()) {
      return found_or.error_value();
    }
    if (found_or.value()) {
      return ZX_ERR_ALREADY_EXISTS;
    }
  }

  // Creating a directory?
  uint32_t type = S_ISDIR(mode) ? kMinfsTypeDir : kMinfsTypeFile;

  // Ensure that we have enough space to write the new vnode's direntry
  // before updating any other metadata.
  {
    TRACE_DURATION("minfs", "Directory::Create::SpaceCheck");
    args.type = type;
    args.reclen = static_cast<uint32_t>(DirentSize(static_cast<uint8_t>(name.length())));
    zx::status<bool> found_or = ForEachDirent(&args, DirentCallbackFindSpace);
    if (found_or.is_error()) {
      return found_or.error_value();
    }
    if (!found_or.value()) {
      FX_LOGS(WARNING) << "Directory::Create: Can't find a dirent to put this file.";
      return ZX_ERR_NO_SPACE;
    }
  }

  // Calculate maximum blocks to reserve for the current directory, based on the size and offset
  // of the new direntry (Assuming that the offset is the current size of the directory).
  auto reserve_blocks_or = GetRequiredBlockCount(GetSize(), args.reclen, Vfs()->BlockSize());
  if (reserve_blocks_or.is_error()) {
    return reserve_blocks_or.error_value();
  }

  // Reserve 1 additional block for the new directory's initial . and .. entries.
  blk_t reserve_blocks = reserve_blocks_or.value() + 1;

  ZX_DEBUG_ASSERT(reserve_blocks <= Vfs()->Limits().GetMaximumMetaDataBlocks());
  zx_status_t status;

  // In addition to reserve_blocks, reserve 1 inode for the vnode to be created.
  auto transaction_or = Vfs()->BeginTransaction(1, reserve_blocks);
  if (transaction_or.is_error()) {
    return transaction_or.error_value();
  }

  // mint a new inode and vnode for it
  auto vn_or = Vfs()->VnodeNew(transaction_or.value().get(), type);
  if (vn_or.is_error()) {
    return vn_or.error_value();
  }

  // If the new node is a directory, fill it with '.' and '..'.
  if (type == kMinfsTypeDir) {
    TRACE_DURATION("minfs", "Directory::Create::InitDir");
    char bdata[DirentSize(1) + DirentSize(2)];
    InitializeDirectory(bdata, vn_or->GetIno(), GetIno());
    size_t expected = DirentSize(1) + DirentSize(2);
    if (auto status = vn_or->WriteExactInternal(transaction_or.value().get(), bdata, expected, 0);
        status.is_error()) {
      FX_LOGS(ERROR) << "Create: Failed to initialize empty directory: " << status.status_value();
      return ZX_ERR_IO;
    }
    vn_or->InodeSync(transaction_or.value().get(), kMxFsSyncDefault);
  }

  // add directory entry for the new child node
  args.ino = vn_or->GetIno();
  args.transaction = transaction_or.value().get();
  if (auto status = AppendDirent(&args); status.is_error()) {
    return status.error_value();
  }

  transaction_or->PinVnode(fbl::RefPtr(this));
  transaction_or->PinVnode(vn_or.value());
  Vfs()->CommitTransaction(std::move(transaction_or.value()));

  if ((status = vn_or->OpenValidating(fs::VnodeConnectionOptions(), nullptr)) != ZX_OK) {
    return status;
  }
  *out = std::move(vn_or.value());
  success = true;
  return ZX_OK;
}

zx_status_t Directory::Unlink(std::string_view name, bool must_be_dir) {
  TRACE_DURATION("minfs", "Directory::Unlink", "name", name);
  ZX_DEBUG_ASSERT(fs::IsValidName(name));
  bool success = false;
  fs::Ticker ticker(Vfs()->StartTicker());
  auto get_metrics = fit::defer(
      [&ticker, &success, this]() { Vfs()->UpdateUnlinkMetrics(success, ticker.End()); });

  auto transaction_or = Vfs()->BeginTransaction(0, 0);
  if (transaction_or.is_error()) {
    return transaction_or.error_value();
  }

  DirArgs args;
  args.name = name;
  args.type = must_be_dir ? kMinfsTypeDir : 0;
  args.transaction = transaction_or.value().get();

  zx::status<bool> found_or = ForEachDirent(&args, DirentCallbackUnlink);
  if (found_or.is_error()) {
    return found_or.error_value();
  }
  if (!found_or.value()) {
    return ZX_ERR_NOT_FOUND;
  }
  transaction_or->PinVnode(fbl::RefPtr(this));
  Vfs()->CommitTransaction(std::move(transaction_or.value()));
  success = true;
  return ZX_OK;
}

zx_status_t Directory::Truncate(size_t len) { return ZX_ERR_NOT_FILE; }

// Verify that the 'newdir' inode is not a subdirectory of the source.
zx::status<> Directory::CheckNotSubdirectory(fbl::RefPtr<Directory> newdir) {
  fbl::RefPtr<Directory> vn = std::move(newdir);
  zx::status<> status = zx::ok();
  while (vn->GetIno() != kMinfsRootIno) {
    if (vn->GetIno() == GetIno()) {
      status = zx::error(ZX_ERR_INVALID_ARGS);
      break;
    }

    auto lookup_or = vn->LookupInternal("..");
    if (lookup_or.is_error()) {
      status = lookup_or.take_error();
      break;
    }
    vn = fbl::RefPtr<Directory>::Downcast(lookup_or.value());
  }
  return status;
}

zx_status_t Directory::Rename(fbl::RefPtr<fs::Vnode> _newdir, std::string_view oldname,
                              std::string_view newname, bool src_must_be_dir,
                              bool dst_must_be_dir) {
  TRACE_DURATION("minfs", "Directory::Rename", "src", oldname, "dst", newname);
  bool success = false;
  fs::Ticker ticker(Vfs()->StartTicker());
  auto get_metrics = fit::defer(
      [&ticker, &success, this]() { Vfs()->UpdateRenameMetrics(success, ticker.End()); });

  ZX_DEBUG_ASSERT(fs::IsValidName(oldname));
  ZX_DEBUG_ASSERT(fs::IsValidName(newname));

  auto newdir_minfs = fbl::RefPtr<VnodeMinfs>::Downcast(_newdir);

  // Ensure that the vnodes containing oldname and newname are directories.
  if (!(newdir_minfs->IsDirectory())) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (newdir_minfs->IsUnlinked()) {
    return ZX_ERR_NOT_FOUND;
  }
  auto newdir = fbl::RefPtr<Directory>::Downcast(newdir_minfs);

  // Acquire the 'oldname' node (it must exist).
  DirArgs args;
  args.name = oldname;

  if (zx::status<bool> found_or = ForEachDirent(&args, DirentCallbackFind); found_or.is_error()) {
    return found_or.error_value();
  } else if (!found_or.value()) {
    return ZX_ERR_NOT_FOUND;
  }

  auto oldvn_or = Vfs()->VnodeGet(args.ino);
  if (oldvn_or.is_error()) {
    return oldvn_or.error_value();
  }
  if (oldvn_or->IsDirectory()) {
    auto olddir = fbl::RefPtr<Directory>::Downcast(oldvn_or.value());
    if (auto status = olddir->CheckNotSubdirectory(newdir); status.is_error()) {
      return status.error_value();
    }
  }

  // If either the 'src' or 'dst' must be directories, BOTH of them must be directories.
  if (!oldvn_or->IsDirectory() && (src_must_be_dir || dst_must_be_dir)) {
    return ZX_ERR_NOT_DIR;
  }
  if ((newdir->GetIno() == GetIno()) && (oldname == newname)) {
    // Renaming a file or directory to itself?
    // Shortcut success case.
    success = true;
    return ZX_OK;
  }

  // Ensure that we have enough space to write the vnode's new direntry
  // before updating any other metadata.
  args.type = oldvn_or->IsDirectory() ? kMinfsTypeDir : kMinfsTypeFile;
  args.reclen = static_cast<uint32_t>(DirentSize(static_cast<uint8_t>(newname.length())));

  if (zx::status<bool> found_or = newdir->ForEachDirent(&args, DirentCallbackFindSpace);
      found_or.is_error()) {
    return found_or.error_value();
  } else if (!found_or.value()) {
    FX_LOGS(WARNING) << "Directory::Rename: Can't find a dirent to put this file.";
    return ZX_ERR_NO_SPACE;
  }

  DirectoryOffset append_offs = args.offs;

  // Reserve potential blocks to add a new direntry to newdir.
  auto reserved_blocks_or =
      GetRequiredBlockCount(newdir->GetInode()->size, args.reclen, Vfs()->BlockSize());
  if (reserved_blocks_or.is_error()) {
    return reserved_blocks_or.error_value();
  }

  auto transaction_or = Vfs()->BeginTransaction(0, reserved_blocks_or.value());
  if (transaction_or.is_error()) {
    return transaction_or.error_value();
  }

  // If the entry for 'newname' exists, make sure it can be replaced by
  // the vnode behind 'oldname'.
  args.transaction = transaction_or.value().get();
  args.name = newname;
  args.ino = oldvn_or->GetIno();

  if (zx::status<bool> found_or = newdir->ForEachDirent(&args, DirentCallbackAttemptRename);
      found_or.is_error()) {
    return found_or.error_value();
  } else if (!found_or.value()) {
    // If 'newname' does not exist, create it.
    args.offs = append_offs;
    if (auto status = newdir->AppendDirent(&args); status.is_error()) {
      return status.error_value();
    }
  }

  // Update the oldvn's entry for '..' if (1) it was a directory, and (2) it moved to a new
  // directory.
  if ((args.type == kMinfsTypeDir) && (GetIno() != newdir->GetIno())) {
    fbl::RefPtr<fs::Vnode> vn_fs;
    if (zx_status_t status = newdir->Lookup(newname, &vn_fs); status < 0) {
      return status;
    }
    auto vn = fbl::RefPtr<Directory>::Downcast(vn_fs);
    args.name = "..";
    args.ino = newdir->GetIno();

    if (zx::status<bool> found_or = vn->ForEachDirent(&args, DirentCallbackUpdateInode);
        found_or.is_error()) {
      return found_or.error_value();
    } else if (!found_or.value()) {
      return ZX_ERR_NOT_FOUND;
    }
  }

  // At this point, the oldvn exists with multiple names (or the same name in different
  // directories).
  oldvn_or->AddLink();

  // finally, remove oldname from its original position
  args.name = oldname;
  zx::status<bool> found_or = ForEachDirent(&args, DirentCallbackForceUnlink);
  if (found_or.is_error()) {
    return found_or.error_value();
  }
  if (!found_or.value()) {
    return ZX_ERR_NOT_FOUND;
  }
  transaction_or->PinVnode(std::move(oldvn_or.value()));
  transaction_or->PinVnode(std::move(newdir));
  Vfs()->CommitTransaction(std::move(transaction_or.value()));
  success = true;
  return ZX_OK;
}

zx_status_t Directory::Link(std::string_view name, fbl::RefPtr<fs::Vnode> _target) {
  TRACE_DURATION("minfs", "Directory::Link", "name", name);
  ZX_DEBUG_ASSERT(fs::IsValidName(name));

  if (IsUnlinked()) {
    return ZX_ERR_BAD_STATE;
  }

  auto target = fbl::RefPtr<VnodeMinfs>::Downcast(_target);
  if (target->IsDirectory()) {
    // The target must not be a directory
    return ZX_ERR_NOT_FILE;
  }

  // The destination should not exist
  DirArgs args;
  args.name = name;
  zx::status<bool> found_or = ForEachDirent(&args, DirentCallbackFind);
  if (found_or.is_error()) {
    return found_or.error_value();
  }
  if (found_or.value()) {
    return ZX_ERR_ALREADY_EXISTS;
  }

  // Ensure that we have enough space to write the new vnode's direntry
  // before updating any other metadata.
  args.type = kMinfsTypeFile;  // We can't hard link directories
  args.reclen = static_cast<uint32_t>(DirentSize(static_cast<uint8_t>(name.length())));
  if (zx::status<bool> found_or = ForEachDirent(&args, DirentCallbackFindSpace);
      found_or.is_error()) {
    return found_or.error_value();
  } else if (!found_or.value()) {
    FX_LOGS(WARNING) << "Directory::Link: Can't find a dirent to put this file.";
    return ZX_ERR_NO_SPACE;
  }

  // Reserve potential blocks to write a new direntry.
  auto reserved_blocks_or =
      GetRequiredBlockCount(GetInode()->size, args.reclen, Vfs()->BlockSize());
  if (reserved_blocks_or.is_error()) {
    return reserved_blocks_or.error_value();
  }

  auto transaction_or = Vfs()->BeginTransaction(0, reserved_blocks_or.value());
  if (transaction_or.is_error()) {
    return transaction_or.error_value();
  }

  args.ino = target->GetIno();
  args.transaction = transaction_or.value().get();
  if (auto status = AppendDirent(&args); status.is_error()) {
    return status.error_value();
  }

  // We have successfully added the vn to a new location. Increment the link count.
  target->AddLink();
  target->InodeSync(transaction_or.value().get(), kMxFsSyncDefault);
  transaction_or->PinVnode(fbl::RefPtr(this));
  transaction_or->PinVnode(target);
  Vfs()->CommitTransaction(std::move(transaction_or.value()));
  return ZX_OK;
}

}  // namespace minfs
