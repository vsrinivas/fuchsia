// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/memfs/vnode.h"

#include "src/storage/memfs/dnode.h"

namespace memfs {

std::atomic<uint64_t> Vnode::ino_ctr_ = 0;
std::atomic<uint64_t> Vnode::deleted_ino_ctr_ = 0;

Vnode::Vnode(PlatformVfs* vfs)
    : fs::Vnode(vfs), ino_(ino_ctr_.fetch_add(1, std::memory_order_relaxed)) {
  ZX_DEBUG_ASSERT(vfs);
  std::timespec ts;
  if (std::timespec_get(&ts, TIME_UTC)) {
    create_time_ = modify_time_ = zx_time_from_timespec(ts);
  }
}

Vnode::~Vnode() { deleted_ino_ctr_.fetch_add(1, std::memory_order_relaxed); }

zx_status_t Vnode::SetAttributes(fs::VnodeAttributesUpdate attr) {
  if (attr.has_modification_time()) {
    modify_time_ = attr.take_modification_time();
  }
  if (attr.any()) {
    // any unhandled field update is unsupported
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

void Vnode::Sync(SyncCallback closure) {
  // Since this filesystem is in-memory, all data is already up-to-date in
  // the underlying storage
  closure(ZX_OK);
}

void Vnode::UpdateModified() {
  std::timespec ts;
  if (std::timespec_get(&ts, TIME_UTC)) {
    modify_time_ = zx_time_from_timespec(ts);
  } else {
    modify_time_ = 0;
  }

#ifdef __Fuchsia__
  // Notify current vnode.
  CheckInotifyFilterAndNotify(fuchsia_io::wire::InotifyWatchMask::kModify);
  // Notify all parent vnodes.
  for (auto parent = dnode_parent_; parent != nullptr; parent = parent->GetParent()) {
    parent->AcquireVnode()->CheckInotifyFilterAndNotify(
        fuchsia_io::wire::InotifyWatchMask::kModify);
  }
#endif
}

}  // namespace memfs
