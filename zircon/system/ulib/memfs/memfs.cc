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
#include <utility>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_ptr.h>
#include <fs/vfs.h>

#include "dnode.h"

namespace memfs {
namespace {

constexpr size_t kPageSize = static_cast<size_t>(PAGE_SIZE);

zx_status_t CreateID(uint64_t* out_id) {
  zx::event id;
  zx_status_t status = zx::event::create(0, &id);
  if (status != ZX_OK) {
    return status;
  }
  zx_info_handle_basic_t info;
  status = id.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    return status;
  }

  *out_id = info.koid;
  return ZX_OK;
}

}  // namespace

zx_status_t Vfs::GrowVMO(zx::vmo& vmo, size_t current_size, size_t request_size,
                         size_t* actual_size) {
  if (request_size <= current_size) {
    *actual_size = current_size;
    return ZX_OK;
  }
  size_t aligned_len = fbl::round_up(request_size, kPageSize);
  ZX_DEBUG_ASSERT(current_size % kPageSize == 0);
  zx_status_t status;
  if (!vmo.is_valid()) {
    if ((status = zx::vmo::create(aligned_len, ZX_VMO_RESIZABLE, &vmo)) != ZX_OK) {
      return status;
    }
  } else {
    if ((status = vmo.set_size(aligned_len)) != ZX_OK) {
      return status;
    }
  }
  // vmo operation succeeded
  *actual_size = aligned_len;
  return ZX_OK;
}

zx_status_t Vfs::Create(const char* name, std::unique_ptr<memfs::Vfs>* out_vfs,
                        fbl::RefPtr<VnodeDir>* out_root) {
  uint64_t id;
  zx_status_t status = CreateID(&id);
  if (status != ZX_OK) {
    return status;
  }

  auto fs = std::unique_ptr<memfs::Vfs>(new memfs::Vfs(id, name));
  fbl::RefPtr<VnodeDir> root = fbl::AdoptRef(new VnodeDir(fs.get()));
  std::unique_ptr<Dnode> dn = Dnode::Create(name, root);
  root->dnode_ = dn.get();
  fs->root_ = std::move(dn);

  *out_root = std::move(root);
  *out_vfs = std::move(fs);
  return ZX_OK;
}

Vfs::Vfs(uint64_t id, const char* name) : fs::ManagedVfs(), fs_id_(id) {}

Vfs::~Vfs() = default;

zx_status_t Vfs::CreateFromVmo(VnodeDir* parent, fbl::StringPiece name, zx_handle_t vmo,
                               zx_off_t off, zx_off_t len) {
  fbl::AutoLock lock(&vfs_lock_);
  return parent->CreateFromVmo(name, vmo, off, len);
}

std::atomic<uint64_t> VnodeMemfs::ino_ctr_ = 0;
std::atomic<uint64_t> VnodeMemfs::deleted_ino_ctr_ = 0;

VnodeMemfs::VnodeMemfs(Vfs* vfs)
    : dnode_(nullptr),
      link_count_(0),
      vfs_(vfs),
      ino_(ino_ctr_.fetch_add(1, std::memory_order_relaxed)),
      create_time_(0),
      modify_time_(0) {
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
  SetRemote(h.TakeChannel());
  return ZX_OK;
}

}  // namespace memfs
