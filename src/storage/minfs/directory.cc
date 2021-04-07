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

// Possible non-error return values for DirentCallback:
//
// Immediately stop iterating over the directory.
constexpr zx_status_t kDirIteratorDone = 0;
// Access the next direntry in the directory. Offsets updated.
constexpr zx_status_t kDirIteratorNext = 1;
// Identify that the direntry record was modified. Stop iterating.
constexpr zx_status_t kDirIteratorSaveSync = 2;

zx_status_t ValidateDirent(Dirent* de, size_t bytes_read, size_t off) {
  if (bytes_read < kMinfsDirentSize) {
    FX_LOGS(ERROR) << "vn_dir: Short read (" << bytes_read << " bytes) at offset " << off;
    return ZX_ERR_IO;
  }
  uint32_t reclen = static_cast<uint32_t>(MinfsReclen(de, off));
  if (reclen < kMinfsDirentSize) {
    FX_LOGS(ERROR) << "vn_dir: Could not read dirent at offset: " << off;
    return ZX_ERR_IO;
  }
  if ((off + reclen > kMinfsMaxDirectorySize) || (reclen & kMinfsDirentAlignmentMask)) {
    FX_LOGS(ERROR) << "vn_dir: bad reclen " << reclen << " > " << kMinfsMaxDirectorySize;
    return ZX_ERR_IO;
  }
  if (de->ino != 0) {
    if ((de->namelen == 0) || (de->namelen > (reclen - kMinfsDirentSize))) {
      FX_LOGS(ERROR) << "vn_dir: bad namelen " << de->namelen << " / " << reclen;
      return ZX_ERR_IO;
    }
  }
  return ZX_OK;
}

// Updates offset information to move to the next direntry in the directory.
zx_status_t NextDirent(Dirent* de, DirectoryOffset* offs) {
  offs->off_prev = offs->off;
  offs->off += MinfsReclen(de, offs->off);
  return kDirIteratorNext;
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

zx_status_t Directory::DirentCallbackFind(fbl::RefPtr<Directory> vndir, Dirent* de, DirArgs* args) {
  if ((de->ino != 0) && std::string_view(de->name, de->namelen) == args->name) {
    args->ino = de->ino;
    args->type = de->type;
    return kDirIteratorDone;
  }
  return NextDirent(de, &args->offs);
}

zx_status_t Directory::CanUnlink() const {
  // directories must be empty (dirent_count == 2)
  if (GetInode()->dirent_count != 2) {
    // if we have more than "." and "..", not empty, cannot unlink
    return ZX_ERR_NOT_EMPTY;
#ifdef __Fuchsia__
  } else if (IsRemote()) {
    // we cannot unlink mount points
    return ZX_ERR_UNAVAILABLE;
#endif
  }
  return ZX_OK;
}

zx_status_t Directory::UnlinkChild(Transaction* transaction, fbl::RefPtr<VnodeMinfs> childvn,
                                   Dirent* de, DirectoryOffset* offs) {
  // Coalesce the current dirent with the previous/next dirent, if they
  // (1) exist and (2) are free.
  size_t off_prev = offs->off_prev;
  size_t off = offs->off;
  size_t off_next = off + MinfsReclen(de, off);
  zx_status_t status;

  // Read the direntries we're considering merging with.
  // Verify they are free and small enough to merge.
  size_t coalesced_size = MinfsReclen(de, off);
  // Coalesce with "next" first, so the kMinfsReclenLast bit can easily flow
  // back to "de" and "de_prev".
  if (!(de->reclen & kMinfsReclenLast)) {
    Dirent de_next;
    size_t len = kMinfsDirentSize;
    if ((status = ReadExactInternal(transaction, &de_next, len, off_next)) != ZX_OK) {
      FX_LOGS(ERROR) << "unlink: Failed to read next dirent";
      return status;
    }
    if ((status = ValidateDirent(&de_next, len, off_next)) != ZX_OK) {
      FX_LOGS(ERROR) << "unlink: Read invalid dirent";
      return status;
    }
    if (de_next.ino == 0) {
      coalesced_size += MinfsReclen(&de_next, off_next);
      // If the next entry *was* last, then 'de' is now last.
      de->reclen |= (de_next.reclen & kMinfsReclenLast);
    }
  }
  if (off_prev != off) {
    Dirent de_prev;
    size_t len = kMinfsDirentSize;
    if ((status = ReadExactInternal(transaction, &de_prev, len, off_prev)) != ZX_OK) {
      FX_LOGS(ERROR) << "unlink: Failed to read previous dirent";
      return status;
    }
    if ((status = ValidateDirent(&de_prev, len, off_prev)) != ZX_OK) {
      FX_LOGS(ERROR) << "unlink: Read invalid dirent";
      return status;
    }
    if (de_prev.ino == 0) {
      coalesced_size += MinfsReclen(&de_prev, off_prev);
      off = off_prev;
    }
  }

  if (!(de->reclen & kMinfsReclenLast) && (coalesced_size >= kMinfsReclenMask)) {
    // Should only be possible if the on-disk record format is corrupted
    FX_LOGS(ERROR) << "unlink: Corrupted direntry with impossibly large size";
    return ZX_ERR_IO;
  }
  de->ino = 0;
  de->reclen =
      static_cast<uint32_t>(coalesced_size & kMinfsReclenMask) | (de->reclen & kMinfsReclenLast);
  // Erase dirent (replace with 'empty' dirent)
  if ((status = WriteExactInternal(transaction, de, kMinfsDirentSize, off)) != ZX_OK) {
    return status;
  }

  if (de->reclen & kMinfsReclenLast) {
    // Truncating the directory merely removed unused space; if it fails,
    // the directory contents are still valid.
    TruncateInternal(transaction, off + kMinfsDirentSize);
  }

  GetMutableInode()->dirent_count--;

  if (MinfsMagicType(childvn->GetInode()->magic) == kMinfsTypeDir) {
    // Child directory had '..' which pointed to parent directory
    GetMutableInode()->link_count--;
  }

  status = childvn->RemoveInodeLink(transaction);
  if (status != ZX_OK) {
    return status;
  }
  transaction->PinVnode(fbl::RefPtr(this));
  transaction->PinVnode(childvn);
  return kDirIteratorSaveSync;
}

// caller is expected to prevent unlink of "." or ".."
zx_status_t Directory::DirentCallbackUnlink(fbl::RefPtr<Directory> vndir, Dirent* de,
                                            DirArgs* args) {
  if ((de->ino == 0) || std::string_view(de->name, de->namelen) != args->name) {
    return NextDirent(de, &args->offs);
  }

  fbl::RefPtr<VnodeMinfs> vn;
  zx_status_t status;
  if ((status = vndir->Vfs()->VnodeGet(&vn, de->ino)) < 0) {
    return status;
  }

  // If a directory was requested, then only try unlinking a directory
  if ((args->type == kMinfsTypeDir) && !vn->IsDirectory()) {
    return ZX_ERR_NOT_DIR;
  }
  if ((status = vn->CanUnlink()) != ZX_OK) {
    return status;
  }
  return vndir->UnlinkChild(args->transaction, std::move(vn), de, &args->offs);
}

// same as unlink, but do not validate vnode
zx_status_t Directory::DirentCallbackForceUnlink(fbl::RefPtr<Directory> vndir, Dirent* de,
                                                 DirArgs* args) {
  if ((de->ino == 0) || std::string_view(de->name, de->namelen) != args->name) {
    return NextDirent(de, &args->offs);
  }

  fbl::RefPtr<VnodeMinfs> vn;
  zx_status_t status;
  if ((status = vndir->Vfs()->VnodeGet(&vn, de->ino)) < 0) {
    return status;
  }
  return vndir->UnlinkChild(args->transaction, std::move(vn), de, &args->offs);
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
zx_status_t Directory::DirentCallbackAttemptRename(fbl::RefPtr<Directory> vndir, Dirent* de,
                                                   DirArgs* args) {
  if ((de->ino == 0) || std::string_view(de->name, de->namelen) != args->name) {
    return NextDirent(de, &args->offs);
  }

  fbl::RefPtr<VnodeMinfs> vn;
  zx_status_t status;
  if ((status = vndir->Vfs()->VnodeGet(&vn, de->ino)) < 0) {
    return status;
  }
  if (args->ino == vn->GetIno()) {
    // cannot rename node to itself
    return ZX_ERR_BAD_STATE;
  }
  if (args->type != de->type) {
    // cannot rename directory to file (or vice versa)
    return args->type == kMinfsTypeDir ? ZX_ERR_NOT_DIR : ZX_ERR_NOT_FILE;
  }
  if ((status = vn->CanUnlink()) != ZX_OK) {
    // if we cannot unlink the target, we cannot rename the target
    return status;
  }

  // If we are renaming ON TOP of a directory, then we can skip
  // updating the parent link count -- the old directory had a ".." entry to
  // the parent (link count of 1), but the new directory will ALSO have a ".."
  // entry, making the rename operation idempotent w.r.t. the parent link
  // count.

  status = vn->RemoveInodeLink(args->transaction);
  if (status != ZX_OK) {
    return status;
  }

  de->ino = args->ino;
  status =
      vndir->WriteExactInternal(args->transaction, de, DirentSize(de->namelen), args->offs.off);
  if (status != ZX_OK) {
    return status;
  }

  args->transaction->PinVnode(vn);
  args->transaction->PinVnode(vndir);
  return kDirIteratorSaveSync;
}

zx_status_t Directory::DirentCallbackUpdateInode(fbl::RefPtr<Directory> vndir, Dirent* de,
                                                 DirArgs* args) {
  if ((de->ino == 0) || std::string_view(de->name, de->namelen) != args->name) {
    return NextDirent(de, &args->offs);
  }

  de->ino = args->ino;
  zx_status_t status =
      vndir->WriteExactInternal(args->transaction, de, DirentSize(de->namelen), args->offs.off);
  if (status != ZX_OK) {
    return status;
  }
  args->transaction->PinVnode(vndir);
  return kDirIteratorSaveSync;
}

zx_status_t Directory::DirentCallbackFindSpace(fbl::RefPtr<Directory> vndir, Dirent* de,
                                               DirArgs* args) {
  uint32_t reclen = static_cast<uint32_t>(MinfsReclen(de, args->offs.off));
  if (de->ino == 0) {
    // empty entry, do we fit?
    if (args->reclen > reclen) {
      return NextDirent(de, &args->offs);
    }
    return kDirIteratorDone;
  }

  // filled entry, can we sub-divide?
  uint32_t size = static_cast<uint32_t>(DirentSize(de->namelen));
  if (size > reclen) {
    FX_LOGS(ERROR) << "bad reclen (smaller than dirent) " << reclen << " < " << size;
    return ZX_ERR_IO;
  }
  uint32_t extra = reclen - size;
  if (extra < args->reclen) {
    return NextDirent(de, &args->offs);
  }
  return kDirIteratorDone;
}

zx_status_t Directory::AppendDirent(DirArgs* args) {
  DirentBuffer dirent_buffer;
  Dirent* de = &dirent_buffer.dirent;

  size_t r;
  zx_status_t status = ReadInternal(args->transaction, de, kMinfsMaxDirentSize, args->offs.off, &r);
  if (status != ZX_OK) {
    return status;
  }

  status = ValidateDirent(de, r, args->offs.off);
  if (status != ZX_OK) {
    return status;
  }

  uint32_t reclen = static_cast<uint32_t>(MinfsReclen(de, args->offs.off));
  if (de->ino == 0) {
    // empty entry, do we fit?
    if (args->reclen > reclen) {
      return ZX_ERR_NO_SPACE;
    }
  } else {
    // filled entry, can we sub-divide?
    uint32_t size = static_cast<uint32_t>(DirentSize(de->namelen));
    if (size > reclen) {
      FX_LOGS(ERROR) << "bad reclen (smaller than dirent) " << reclen << " < " << size;
      return ZX_ERR_IO;
    }
    uint32_t extra = reclen - size;
    if (extra < args->reclen) {
      return ZX_ERR_NO_SPACE;
    }
    // shrink existing entry
    bool was_last_record = de->reclen & kMinfsReclenLast;
    de->reclen = size;
    if ((status = WriteExactInternal(args->transaction, de, DirentSize(de->namelen),
                                     args->offs.off)) != ZX_OK) {
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
  if ((status = WriteExactInternal(args->transaction, de, DirentSize(de->namelen),
                                   args->offs.off)) != ZX_OK) {
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
  return ZX_OK;
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
    zx_status_t status =
        ReadInternal(args->transaction, de, kMinfsMaxDirentSize, args->offs.off, &r);
    if (status != ZX_OK) {
      return zx::error(status);
    }
    status = ValidateDirent(de, r, args->offs.off);
    if (status != ZX_OK) {
      return zx::error(status);
    }

    switch ((status = func(fbl::RefPtr<Directory>(this), de, args))) {
      case kDirIteratorNext:
        break;
      case kDirIteratorSaveSync:
        GetMutableInode()->seq_num++;
        InodeSync(args->transaction, kMxFsSyncMtime);
        args->transaction->PinVnode(fbl::RefPtr(this));
        return zx::ok(true);
      case kDirIteratorDone:
        return zx::ok(true);
      default:
        // All errors. The callback should not be returning any other non-error (positive) values.
        ZX_DEBUG_ASSERT(status < 0);
        return zx::error(status);
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
  ZX_DEBUG_ASSERT(fs::vfs_valid_name(name));

  return LookupInternal(out, name);
}

zx_status_t Directory::LookupInternal(fbl::RefPtr<fs::Vnode>* out, std::string_view name) {
  DirArgs args;
  args.name = name;

  bool success = false;
  fs::Ticker ticker(Vfs()->StartTicker());
  auto get_metrics = fit::defer(
      [&ticker, &success, this]() { Vfs()->UpdateLookupMetrics(success, ticker.End()); });

  if (zx::status<bool> found_or = ForEachDirent(&args, DirentCallbackFind); found_or.is_error()) {
    return found_or.error_value();
  } else if (!found_or.value()) {
    return ZX_ERR_NOT_FOUND;
  }
  fbl::RefPtr<VnodeMinfs> vn;
  if (zx_status_t status = Vfs()->VnodeGet(&vn, args.ino); status != ZX_OK) {
    return status;
  }
  *out = std::move(vn);
  return ZX_OK;
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
      zx_status_t status = ReadInternal(nullptr, de, kMinfsMaxDirentSize, off_recovered, &r);
      if ((status != ZX_OK) || (ValidateDirent(de, r, off_recovered) != ZX_OK)) {
        FX_LOGS(ERROR) << "Readdir: Corrupt dirent unreadable/failed validation";
        goto fail;
      }
      off_recovered += MinfsReclen(de, off_recovered);
    }
    off = off_recovered;
  }

  while (off + kMinfsDirentSize < kMinfsMaxDirectorySize) {
    zx_status_t status = ReadInternal(nullptr, de, kMinfsMaxDirentSize, off, &r);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Readdir: Unreadable dirent " << status;
      goto fail;
    } else if ((status = ValidateDirent(de, r, off)) != ZX_OK) {
      FX_LOGS(ERROR) << "Readdir: Corrupt dirent failed validation " << status;
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

    off += MinfsReclen(de, off);
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

  if (!fs::vfs_valid_name(name)) {
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
  std::unique_ptr<Transaction> transaction;
  if ((status = Vfs()->BeginTransaction(1, reserve_blocks, &transaction)) != ZX_OK) {
    return status;
  }

  // mint a new inode and vnode for it
  fbl::RefPtr<VnodeMinfs> vn;
  if ((status = Vfs()->VnodeNew(transaction.get(), &vn, type)) < 0) {
    return status;
  }

  // If the new node is a directory, fill it with '.' and '..'.
  if (type == kMinfsTypeDir) {
    TRACE_DURATION("minfs", "Directory::Create::InitDir");
    char bdata[DirentSize(1) + DirentSize(2)];
    InitializeDirectory(bdata, vn->GetIno(), GetIno());
    size_t expected = DirentSize(1) + DirentSize(2);
    if ((status = vn->WriteExactInternal(transaction.get(), bdata, expected, 0)) != ZX_OK) {
      FX_LOGS(ERROR) << "Create: Failed to initialize empty directory: " << status;
      return ZX_ERR_IO;
    }
    vn->InodeSync(transaction.get(), kMxFsSyncDefault);
  }

  // add directory entry for the new child node
  args.ino = vn->GetIno();
  args.transaction = transaction.get();
  if ((status = AppendDirent(&args)) != ZX_OK) {
    return status;
  }

  transaction->PinVnode(fbl::RefPtr(this));
  transaction->PinVnode(vn);
  Vfs()->CommitTransaction(std::move(transaction));

  if ((status = vn->OpenValidating(fs::VnodeConnectionOptions(), nullptr)) != ZX_OK) {
    return status;
  }
  *out = std::move(vn);
  success = true;
  return ZX_OK;
}

zx_status_t Directory::Unlink(std::string_view name, bool must_be_dir) {
  TRACE_DURATION("minfs", "Directory::Unlink", "name", name);
  ZX_DEBUG_ASSERT(fs::vfs_valid_name(name));
  bool success = false;
  fs::Ticker ticker(Vfs()->StartTicker());
  auto get_metrics = fit::defer(
      [&ticker, &success, this]() { Vfs()->UpdateUnlinkMetrics(success, ticker.End()); });

  zx_status_t status;
  std::unique_ptr<Transaction> transaction;
  if ((status = Vfs()->BeginTransaction(0, 0, &transaction)) != ZX_OK) {
    return status;
  }

  DirArgs args;
  args.name = name;
  args.type = must_be_dir ? kMinfsTypeDir : 0;
  args.transaction = transaction.get();

  zx::status<bool> found_or = ForEachDirent(&args, DirentCallbackUnlink);
  if (found_or.is_error()) {
    return found_or.error_value();
  }
  if (!found_or.value()) {
    return ZX_ERR_NOT_FOUND;
  }
  transaction->PinVnode(fbl::RefPtr(this));
  Vfs()->CommitTransaction(std::move(transaction));
  success = true;
  return ZX_OK;
}

zx_status_t Directory::Truncate(size_t len) { return ZX_ERR_NOT_FILE; }

// Verify that the 'newdir' inode is not a subdirectory of the source.
zx_status_t Directory::CheckNotSubdirectory(fbl::RefPtr<Directory> newdir) {
  fbl::RefPtr<Directory> vn = std::move(newdir);
  zx_status_t status = ZX_OK;
  while (vn->GetIno() != kMinfsRootIno) {
    if (vn->GetIno() == GetIno()) {
      status = ZX_ERR_INVALID_ARGS;
      break;
    }

    fbl::RefPtr<fs::Vnode> out = nullptr;
    if ((status = vn->LookupInternal(&out, "..")) < 0) {
      break;
    }
    vn = fbl::RefPtr<Directory>::Downcast(out);
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

  ZX_DEBUG_ASSERT(fs::vfs_valid_name(oldname));
  ZX_DEBUG_ASSERT(fs::vfs_valid_name(newname));

  auto newdir_minfs = fbl::RefPtr<VnodeMinfs>::Downcast(_newdir);

  // Ensure that the vnodes containing oldname and newname are directories.
  if (!(newdir_minfs->IsDirectory())) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (newdir_minfs->IsUnlinked()) {
    return ZX_ERR_NOT_FOUND;
  }
  auto newdir = fbl::RefPtr<Directory>::Downcast(newdir_minfs);

  fbl::RefPtr<VnodeMinfs> oldvn = nullptr;

  // Acquire the 'oldname' node (it must exist).
  DirArgs args;
  args.name = oldname;
  if (zx::status<bool> found_or = ForEachDirent(&args, DirentCallbackFind); found_or.is_error()) {
    return found_or.error_value();
  } else if (!found_or.value()) {
    return ZX_ERR_NOT_FOUND;
  }
  zx_status_t status;
  if ((status = Vfs()->VnodeGet(&oldvn, args.ino)) < 0) {
    return status;
  }
  if (oldvn->IsDirectory()) {
    auto olddir = fbl::RefPtr<Directory>::Downcast(oldvn);
    if ((status = olddir->CheckNotSubdirectory(newdir)) < 0) {
      return status;
    }
  }

  // If either the 'src' or 'dst' must be directories, BOTH of them must be directories.
  if (!oldvn->IsDirectory() && (src_must_be_dir || dst_must_be_dir)) {
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
  args.type = oldvn->IsDirectory() ? kMinfsTypeDir : kMinfsTypeFile;
  args.reclen = static_cast<uint32_t>(DirentSize(static_cast<uint8_t>(newname.length())));

  if (zx::status<bool> found_or = newdir->ForEachDirent(&args, DirentCallbackFindSpace);
      found_or.is_error()) {
    return found_or.error_value();
  } else if (!found_or.value()) {
    return ZX_ERR_NO_SPACE;
  }

  DirectoryOffset append_offs = args.offs;

  // Reserve potential blocks to add a new direntry to newdir.
  auto reserved_blocks_or =
      GetRequiredBlockCount(newdir->GetInode()->size, args.reclen, Vfs()->BlockSize());
  if (reserved_blocks_or.is_error()) {
    return reserved_blocks_or.error_value();
  }

  std::unique_ptr<Transaction> transaction;
  if ((status = Vfs()->BeginTransaction(0, reserved_blocks_or.value(), &transaction)) != ZX_OK) {
    return status;
  }

  // If the entry for 'newname' exists, make sure it can be replaced by
  // the vnode behind 'oldname'.
  args.transaction = transaction.get();
  args.name = newname;
  args.ino = oldvn->GetIno();
  if (zx::status<bool> found_or = newdir->ForEachDirent(&args, DirentCallbackAttemptRename);
      found_or.is_error()) {
    return found_or.error_value();
  } else if (!found_or.value()) {
    // If 'newname' does not exist, create it.
    args.offs = append_offs;
    if ((status = newdir->AppendDirent(&args)) != ZX_OK) {
      return status;
    }
  }

  // Update the oldvn's entry for '..' if (1) it was a directory, and (2) it moved to a new
  // directory.
  if ((args.type == kMinfsTypeDir) && (GetIno() != newdir->GetIno())) {
    fbl::RefPtr<fs::Vnode> vn_fs;
    if ((status = newdir->Lookup(newname, &vn_fs)) < 0) {
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
  oldvn->AddLink();

  // finally, remove oldname from its original position
  args.name = oldname;
  if (zx::status<bool> found_or = ForEachDirent(&args, DirentCallbackForceUnlink);
      found_or.is_error()) {
    return found_or.error_value();
  } else if (!found_or.value()) {
    return ZX_ERR_NOT_FOUND;
  }
  transaction->PinVnode(oldvn);
  transaction->PinVnode(newdir);
  Vfs()->CommitTransaction(std::move(transaction));
  success = true;
  return ZX_OK;
}

zx_status_t Directory::Link(std::string_view name, fbl::RefPtr<fs::Vnode> _target) {
  TRACE_DURATION("minfs", "Directory::Link", "name", name);
  ZX_DEBUG_ASSERT(fs::vfs_valid_name(name));

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
  if (zx::status<bool> found_or = ForEachDirent(&args, DirentCallbackFind); found_or.is_error()) {
    return found_or.error_value();
  } else if (found_or.value()) {
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
    return ZX_ERR_NO_SPACE;
  }

  // Reserve potential blocks to write a new direntry.
  auto reserved_blocks_or =
      GetRequiredBlockCount(GetInode()->size, args.reclen, Vfs()->BlockSize());
  if (reserved_blocks_or.is_error()) {
    return reserved_blocks_or.error_value();
  }

  zx_status_t status;
  std::unique_ptr<Transaction> transaction;
  if ((status = Vfs()->BeginTransaction(0, reserved_blocks_or.value(), &transaction)) != ZX_OK) {
    return status;
  }

  args.ino = target->GetIno();
  args.transaction = transaction.get();
  if ((status = AppendDirent(&args)) != ZX_OK) {
    return status;
  }

  // We have successfully added the vn to a new location. Increment the link count.
  target->AddLink();
  target->InodeSync(transaction.get(), kMxFsSyncDefault);
  transaction->PinVnode(fbl::RefPtr(this));
  transaction->PinVnode(target);
  Vfs()->CommitTransaction(std::move(transaction));
  return ZX_OK;
}

}  // namespace minfs
