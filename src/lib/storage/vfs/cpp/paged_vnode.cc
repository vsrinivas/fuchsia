// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/paged_vnode.h"

#include "src/lib/storage/vfs/cpp/paged_vfs.h"

namespace fs {

PagedVnode::PagedVnode(PagedVfs* vfs) : Vnode(vfs), clone_watcher_(this) {}

PagedVnode::~PagedVnode() {}

zx::status<> PagedVnode::EnsureCreateVmo(uint64_t size) {
  if (vmo_) {
    // The derived class can choose to cache the VMO even if it has no clones, so just because the
    // VMO exists doesn't mean it has clones. As a result, we may need to arm the watcher.
    WatchForZeroVmoClones();

    return zx::ok();
  }

  if (!paged_vfs())
    return zx::error(ZX_ERR_BAD_STATE);  // Currently shutting down.

  auto vfs_or = paged_vfs()->CreatePagedNodeVmo(fbl::RefPtr<PagedVnode>(this), size);
  if (vfs_or.is_error())
    return vfs_or.take_error();
  vmo_ = std::move(vfs_or).value();

  // Watch the VMO for the presence of no children. The VMO currently has no children because we
  // just created it, but the signal will be edge-triggered.
  WatchForZeroVmoClones();

  return zx::ok();
}

void PagedVnode::OnNoClones() {
  // It is now save to release the VMO. Since we know there are no clones, we don't have to
  // call zx_pager_detach_vmo() to stop delivery of requests.
  vmo_.reset();
}

void PagedVnode::OnNoClonesMessage(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                   zx_status_t status, const zx_packet_signal_t* signal) {
  std::lock_guard<std::mutex> lock(mutex_);

  ZX_DEBUG_ASSERT(has_clones_);

  if (!paged_vfs())
    return;  // Called during tear-down.

  // The kernel signal delivery could have raced with us creating a new clone. Validate that there
  // are still no clones before tearing down.
  zx_info_vmo_t info;
  if (vmo_.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr) != ZX_OK)
    return;  // Something wrong with the VMO, don't try to tear down.
  if (info.num_children > 0) {
    // Race with new VMO. Re-arm the clone watcher and continue as if the signal was not sent.
    WatchForZeroVmoClones();
    return;
  }

  clone_watcher_.set_object(ZX_HANDLE_INVALID);
  has_clones_ = false;

  OnNoClones();
}

void PagedVnode::WatchForZeroVmoClones() {
  has_clones_ = true;

  clone_watcher_.set_object(vmo_.get());
  clone_watcher_.set_trigger(ZX_VMO_ZERO_CHILDREN);
  clone_watcher_.Begin(paged_vfs()->dispatcher());
}

}  // namespace fs
