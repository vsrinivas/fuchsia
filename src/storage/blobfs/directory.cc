// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "directory.h"

#include <fuchsia/device/c/fidl.h>
#include <lib/fidl-utils/bind.h>
#include <lib/sync/completion.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/device/vfs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <utility>

#include <digest/digest.h>
#include <fbl/ref_ptr.h>
#include <fbl/string_piece.h>
#include <fs/metrics/events.h>
#include <fs/vfs_types.h>

#include "blob.h"
#include "blobfs.h"
#include "metrics.h"

namespace blobfs {

Directory::Directory(Blobfs* bs) : blobfs_(bs) {}

BlobCache& Directory::Cache() { return blobfs_->Cache(); }

Directory::~Directory() = default;

zx_status_t Directory::GetNodeInfoForProtocol([[maybe_unused]] fs::VnodeProtocol protocol,
                                              [[maybe_unused]] fs::Rights rights,
                                              fs::VnodeRepresentation* info) {
  *info = fs::VnodeRepresentation::Directory();
  return ZX_OK;
}

fs::VnodeProtocolSet Directory::GetProtocols() const { return fs::VnodeProtocol::kDirectory; }

zx_status_t Directory::Readdir(fs::vdircookie_t* cookie, void* dirents, size_t len,
                               size_t* out_actual) {
  return blobfs_->Readdir(cookie, dirents, len, out_actual);
}

zx_status_t Directory::Read(void* data, size_t len, size_t off, size_t* out_actual) {
  return ZX_ERR_NOT_FILE;
}

zx_status_t Directory::Write(const void* data, size_t len, size_t offset, size_t* out_actual) {
  return ZX_ERR_NOT_FILE;
}

zx_status_t Directory::Append(const void* data, size_t len, size_t* out_end, size_t* out_actual) {
  return ZX_ERR_NOT_FILE;
}

zx_status_t Directory::Lookup(fbl::StringPiece name, fbl::RefPtr<fs::Vnode>* out) {
  TRACE_DURATION("blobfs", "Directory::Lookup", "name", name);
  auto event = blobfs_->Metrics()->NewLatencyEvent(fs_metrics::Event::kLookUp);
  assert(memchr(name.data(), '/', name.length()) == nullptr);
  if (name == ".") {
    // Special case: Accessing root directory via '.'
    *out = fbl::RefPtr<Directory>(this);
    return ZX_OK;
  }

  zx_status_t status;
  Digest digest;
  if ((status = digest.Parse(name.data(), name.length())) != ZX_OK) {
    return status;
  }
  fbl::RefPtr<CacheNode> cache_node;
  if ((status = Cache().Lookup(digest, &cache_node)) != ZX_OK) {
    return status;
  }
  auto vnode = fbl::RefPtr<Blob>::Downcast(std::move(cache_node));
  blobfs_->Metrics()->UpdateLookup(vnode->SizeData());
  *out = std::move(vnode);
  return ZX_OK;
}

zx_status_t Directory::GetAttributes(fs::VnodeAttributes* a) {
  *a = fs::VnodeAttributes();
  a->mode = V_TYPE_DIR | V_IRUSR;
  a->inode = ::llcpp::fuchsia::io::INO_UNKNOWN;
  a->content_size = 0;
  a->storage_size = 0;
  a->link_count = 1;
  a->creation_time = 0;
  a->modification_time = 0;
  return ZX_OK;
}

zx_status_t Directory::Create(fbl::StringPiece name, uint32_t mode, fbl::RefPtr<fs::Vnode>* out) {
  TRACE_DURATION("blobfs", "Directory::Create", "name", name, "mode", mode);
  auto event = blobfs_->Metrics()->NewLatencyEvent(fs_metrics::Event::kCreate);
  assert(memchr(name.data(), '/', name.length()) == nullptr);

  Digest digest;
  zx_status_t status;
  if ((status = digest.Parse(name.data(), name.length())) != ZX_OK) {
    return status;
  }

  fbl::RefPtr<Blob> vn = fbl::AdoptRef(new Blob(blobfs_, std::move(digest)));
  if ((status = Cache().Add(vn)) != ZX_OK) {
    return status;
  }
  if ((status = vn->OpenValidating(fs::VnodeConnectionOptions(), nullptr)) != ZX_OK) {
    return status;
  }
  *out = std::move(vn);
  return ZX_OK;
}

#ifdef __Fuchsia__

zx_status_t Directory::QueryFilesystem(::llcpp::fuchsia::io::FilesystemInfo* info) {
  blobfs_->GetFilesystemInfo(info);
  return ZX_OK;
}

zx_status_t Directory::GetDevicePath(size_t buffer_len, char* out_name, size_t* out_len) {
  return blobfs_->Device()->GetDevicePath(buffer_len, out_name, out_len);
}

#endif  // __Fuchsia__

zx_status_t Directory::Unlink(fbl::StringPiece name, bool must_be_dir) {
  TRACE_DURATION("blobfs", "Directory::Unlink", "name", name, "must_be_dir", must_be_dir);
  auto event = blobfs_->Metrics()->NewLatencyEvent(fs_metrics::Event::kUnlink);
  assert(memchr(name.data(), '/', name.length()) == nullptr);

  zx_status_t status;
  Digest digest;
  if ((status = digest.Parse(name.data(), name.length())) != ZX_OK) {
    return status;
  }
  fbl::RefPtr<CacheNode> cache_node;
  if ((status = Cache().Lookup(digest, &cache_node)) != ZX_OK) {
    return status;
  }
  auto vnode = fbl::RefPtr<Blob>::Downcast(std::move(cache_node));
  blobfs_->Metrics()->UpdateLookup(vnode->SizeData());
  return vnode->QueueUnlink();
}

void Directory::Sync(SyncCallback closure) {
  blobfs_->Sync([this, cb = std::move(closure)](zx_status_t status) mutable {
    // This callback will be issued on the journal thread in the normal case. This is important
    // because the WriteTxn must happen there or it will block the main thread which would block
    // processing other requests.
    //
    // If called during shutdown this may get issued on the main thread but then the flush
    // transaction should be a no-op.
    if (status == ZX_OK) {
      fs::WriteTxn sync_txn(blobfs_);
      sync_txn.EnqueueFlush();
      status = sync_txn.Transact();
    }
    cb(status);
  });
}

#ifdef __Fuchsia__

void Directory::HandleFsSpecificMessage(fidl_msg_t* msg, fidl::Transaction* txn) {
  llcpp::fuchsia::blobfs::Blobfs::Dispatch(this, msg, txn);
}

void Directory::GetAllocatedRegions(GetAllocatedRegionsCompleter::Sync completer) {
  static_assert(sizeof(llcpp::fuchsia::blobfs::BlockRegion) == sizeof(BlockRegion));
  static_assert(offsetof(llcpp::fuchsia::blobfs::BlockRegion, offset) ==
                offsetof(BlockRegion, offset));
  static_assert(offsetof(llcpp::fuchsia::blobfs::BlockRegion, length) ==
                offsetof(BlockRegion, length));
  zx::vmo vmo;
  zx_status_t status = ZX_OK;
  fbl::Vector<BlockRegion> buffer = blobfs_->GetAllocator()->GetAllocatedRegions();
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
  }
}

void Directory::SetCorruptBlobHandler(zx::channel handler,
                                      SetCorruptBlobHandlerCompleter::Sync completer) {
  blobfs_->SetCorruptBlobHandler(std::move(handler));
  completer.Reply(ZX_OK);
}

#endif  // __Fuchsia__

}  // namespace blobfs
