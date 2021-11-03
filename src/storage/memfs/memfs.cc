// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <inttypes.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/vfs.h>
#include <lib/memfs/cpp/vnode.h>
#include <lib/memfs/memfs.h>
#include <lib/sync/completion.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zircon/device/vfs.h>
#include <zircon/time.h>

#include <atomic>
#include <ctime>
#include <string_view>
#include <utility>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/ref_ptr.h>

#include "dnode.h"
#include "src/lib/storage/vfs/cpp/vfs.h"

namespace memfs {

size_t GetPageSize() {
  static const size_t kPageSize = static_cast<size_t>(zx_system_get_page_size());
  return kPageSize;
}

zx_status_t Vfs::GrowVMO(zx::vmo& vmo, size_t current_size, size_t request_size,
                         size_t* actual_size) {
  if (request_size <= current_size) {
    *actual_size = current_size;
    return ZX_OK;
  }

  size_t page_size = GetPageSize();
  size_t aligned_len = fbl::round_up(request_size, page_size);
  ZX_DEBUG_ASSERT(current_size % page_size == 0);

  if (!vmo.is_valid()) {
    if (zx_status_t status = zx::vmo::create(aligned_len, ZX_VMO_RESIZABLE, &vmo);
        status != ZX_OK) {
      return status;
    }
  } else {
    if (zx_status_t status = vmo.set_size(aligned_len); status != ZX_OK) {
      return status;
    }
  }
  // vmo operation succeeded
  *actual_size = aligned_len;
  return ZX_OK;
}

zx_status_t Vfs::GetFilesystemInfo(fidl::AnyArena& allocator,
                                   fuchsia_fs::wire::FilesystemInfo& out) {
  out.set_block_size(GetPageSize());
  out.set_max_node_name_size(kDnodeNameMax);
  out.set_fs_type(fuchsia_fs::wire::FsType::kMemfs);

  zx::event fs_id_copy;
  if (fs_id_.duplicate(ZX_RIGHTS_BASIC, &fs_id_copy) == ZX_OK)
    out.set_fs_id(std::move(fs_id_copy));

  // There's no sensible value to use for the total_bytes for memfs. Fuchsia overcommits memory,
  // which means you can have a memfs that stores more total bytes than the device has physical
  // memory. You can actually commit more total_bytes than the device has physical memory because
  // of zero-page deduplication.
  out.set_total_bytes(allocator, UINT64_MAX);
  // It's also very difficult to come up with a sensible value for used_bytes because memfs vends
  // writable duplicates of its underlying VMOs to its client. The client can manipulate the VMOs
  // in arbitrarily difficult ways to account for their memory usage.
  out.set_used_bytes(allocator, 0);
  out.set_total_nodes(allocator, UINT64_MAX);
  uint64_t deleted_ino_count = VnodeMemfs::GetDeletedInoCounter();
  uint64_t ino_count = VnodeMemfs::GetInoCounter();
  ZX_DEBUG_ASSERT(ino_count >= deleted_ino_count);
  out.set_used_nodes(allocator, ino_count - deleted_ino_count);
  out.set_name(allocator, fidl::StringView(allocator, "memfs"));

  return ZX_OK;
}

zx_status_t Vfs::Create(async_dispatcher_t* dispatcher, std::string_view fs_name,
                        std::unique_ptr<memfs::Vfs>* out_vfs, fbl::RefPtr<VnodeDir>* out_root) {
  auto fs = std::unique_ptr<memfs::Vfs>(new memfs::Vfs(dispatcher));

  fbl::RefPtr<VnodeDir> root = fbl::MakeRefCounted<VnodeDir>(fs.get());
  std::unique_ptr<Dnode> dn = Dnode::Create(fs_name, root);
  root->dnode_ = dn.get();
  root->dnode_parent_ = dn.get()->GetParent();
  fs->root_ = std::move(dn);

  if (zx_status_t status = zx::event::create(0, &fs->fs_id_); status != ZX_OK)
    return status;

  *out_root = std::move(root);
  *out_vfs = std::move(fs);
  return ZX_OK;
}

Vfs::Vfs(async_dispatcher_t* dispatcher) : fs::ManagedVfs(dispatcher) {}

Vfs::~Vfs() = default;

zx_status_t Vfs::CreateFromVmo(VnodeDir* parent, std::string_view name, zx_handle_t vmo,
                               zx_off_t off, zx_off_t len) {
  std::lock_guard<std::mutex> lock(vfs_lock_);
  return parent->CreateFromVmo(name, vmo, off, len);
}

std::atomic<uint64_t> VnodeMemfs::ino_ctr_ = 0;
std::atomic<uint64_t> VnodeMemfs::deleted_ino_ctr_ = 0;

VnodeMemfs::VnodeMemfs(PlatformVfs* vfs)
    : Vnode(vfs), ino_(ino_ctr_.fetch_add(1, std::memory_order_relaxed)) {
  ZX_DEBUG_ASSERT(vfs);
  std::timespec ts;
  if (std::timespec_get(&ts, TIME_UTC)) {
    create_time_ = modify_time_ = zx_time_from_timespec(ts);
  }
}

VnodeMemfs::~VnodeMemfs() { deleted_ino_ctr_.fetch_add(1, std::memory_order_relaxed); }

zx_status_t VnodeMemfs::SetAttributes(fs::VnodeAttributesUpdate attr) {
  if (attr.has_modification_time()) {
    modify_time_ = attr.take_modification_time();
  }
  if (attr.any()) {
    // any unhandled field update is unsupported
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

void VnodeMemfs::Sync(SyncCallback closure) {
  // Since this filesystem is in-memory, all data is already up-to-date in
  // the underlying storage
  closure(ZX_OK);
}

zx_status_t VnodeMemfs::AttachRemote(fs::MountChannel h) {
  if (!IsDirectory()) {
    return ZX_ERR_NOT_DIR;
  } else if (IsRemote()) {
    return ZX_ERR_ALREADY_BOUND;
  }
  SetRemote(std::move(h.client_end()));
  return ZX_OK;
}

void VnodeMemfs::UpdateModified() {
  std::timespec ts;
  if (std::timespec_get(&ts, TIME_UTC)) {
    modify_time_ = zx_time_from_timespec(ts);
  } else {
    modify_time_ = 0;
  }

#ifdef __Fuchsia__
  // Notify current vnode.
  CheckInotifyFilterAndNotify(fio2::wire::InotifyWatchMask::kModify);
  // Notify all parent vnodes.
  for (auto parent = dnode_parent_; parent != nullptr; parent = parent->GetParent()) {
    parent->AcquireVnode()->CheckInotifyFilterAndNotify(fio2::wire::InotifyWatchMask::kModify);
  }
#endif
}

}  // namespace memfs
