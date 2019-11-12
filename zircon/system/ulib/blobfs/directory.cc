// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "directory.h"

#include <fuchsia/device/c/fidl.h>
#include <fuchsia/io/c/fidl.h>
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

zx_status_t Directory::Lookup(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name) {
  TRACE_DURATION("blobfs", "Directory::Lookup", "name", name);
  auto event = blobfs_->Metrics().NewLatencyEvent(fs_metrics::Event::kLookUp);
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
  blobfs_->Metrics().UpdateLookup(vnode->SizeData());
  *out = std::move(vnode);
  return ZX_OK;
}

zx_status_t Directory::GetAttributes(fs::VnodeAttributes* a) {
  *a = fs::VnodeAttributes();
  a->mode = V_TYPE_DIR | V_IRUSR;
  a->inode = fuchsia_io_INO_UNKNOWN;
  a->content_size = 0;
  a->storage_size = 0;
  a->link_count = 1;
  a->creation_time = 0;
  a->modification_time = 0;
  return ZX_OK;
}

zx_status_t Directory::Create(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name, uint32_t mode) {
  TRACE_DURATION("blobfs", "Directory::Create", "name", name, "mode", mode);
  auto event = blobfs_->Metrics().NewLatencyEvent(fs_metrics::Event::kCreate);
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

constexpr const char kFsName[] = "blobfs";

zx_status_t Directory::QueryFilesystem(fuchsia_io_FilesystemInfo* info) {
  static_assert(fbl::constexpr_strlen(kFsName) + 1 < fuchsia_io_MAX_FS_NAME_BUFFER,
                "Blobfs name too long");

  memset(info, 0, sizeof(*info));
  info->block_size = kBlobfsBlockSize;
  info->max_filename_size = digest::kSha256HexLength;
  info->fs_type = VFS_TYPE_BLOBFS;
  info->fs_id = blobfs_->GetFsId();
  info->total_bytes = blobfs_->Info().data_block_count * blobfs_->Info().block_size;
  info->used_bytes = blobfs_->Info().alloc_block_count * blobfs_->Info().block_size;
  info->total_nodes = blobfs_->Info().inode_count;
  info->used_nodes = blobfs_->Info().alloc_inode_count;
  strlcpy(reinterpret_cast<char*>(info->name), kFsName, fuchsia_io_MAX_FS_NAME_BUFFER);
  return ZX_OK;
}

zx_status_t Directory::GetDevicePath(size_t buffer_len, char* out_name, size_t* out_len) {
  return blobfs_->Device()->GetDevicePath(buffer_len, out_name, out_len);
}
#endif

zx_status_t Directory::Unlink(fbl::StringPiece name, bool must_be_dir) {
  TRACE_DURATION("blobfs", "Directory::Unlink", "name", name, "must_be_dir", must_be_dir);
  auto event = blobfs_->Metrics().NewLatencyEvent(fs_metrics::Event::kUnlink);
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
  blobfs_->Metrics().UpdateLookup(vnode->SizeData());
  return vnode->QueueUnlink();
}

void Directory::Sync(SyncCallback closure) {
  blobfs_->Sync([this, cb = std::move(closure)](zx_status_t status) {
    if (status != ZX_OK) {
      cb(status);
      return;
    }

    fs::WriteTxn sync_txn(blobfs_);
    sync_txn.EnqueueFlush();
    status = sync_txn.Transact();
    cb(status);
  });
}

#ifdef __Fuchsia__

const fuchsia_blobfs_Blobfs_ops* Directory::Ops() {
  using DirectoryConnectionBinder = fidl::Binder<Directory>;
  static const fuchsia_blobfs_Blobfs_ops kBlobfsOps = {
      .GetAllocatedRegions = DirectoryConnectionBinder::BindMember<&Directory::GetAllocatedRegions>,
  };
  return &kBlobfsOps;
}

zx_status_t Directory::HandleFsSpecificMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  return fuchsia_blobfs_Blobfs_dispatch(this, txn, msg, Ops());
}

#endif  // __Fuchsia__

zx_status_t Directory::GetAllocatedRegions(fidl_txn_t* txn) const {
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
  return fuchsia_blobfs_BlobfsGetAllocatedRegions_reply(
      txn, status, status == ZX_OK ? vmo.get() : ZX_HANDLE_INVALID,
      status == ZX_OK ? allocations : 0);
}

}  // namespace blobfs
