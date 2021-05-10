// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/paged_vnode.h"

#include "src/lib/storage/vfs/cpp/paged_vfs.h"

namespace fs {

PagedVnode::PagedVnode(PagedVfs* vfs) : Vnode(vfs), clone_watcher_(this) {}

PagedVnode::~PagedVnode() {}

zx::status<> PagedVnode::EnsureCreatePagedVmo(uint64_t size) {
  if (paged_vmo())
    return zx::ok();

  if (!paged_vfs())
    return zx::error(ZX_ERR_BAD_STATE);  // Currently shutting down.

  auto info_or = paged_vfs()->CreatePagedNodeVmo(size);
  if (info_or.is_error())
    return info_or.take_error();
  paged_vmo_info_ = std::move(info_or).value();

  return zx::ok();
}

void PagedVnode::EnsurePagedVmoRegistered() {
  // The paged VMO must have been created before registering for notifications.
  ZX_DEBUG_ASSERT(paged_vmo());

  if (is_registered_with_pager_)
    return;

  WatchForZeroVmoClones();
  paged_vfs()->RegisterPagedVmo(paged_vmo_info_.id, fbl::RefPtr<PagedVnode>(this));
  is_registered_with_pager_ = true;
}

void PagedVnode::EnsurePagedVmoUnregistered() {
  if (!is_registered_with_pager_ || !paged_vfs())
    return;

  StopWatchingForZeroVmoClones();
  paged_vfs()->UnregisterPagedVmo(paged_vmo_info_.id);
  is_registered_with_pager_ = false;
}

void PagedVnode::FreePagedVmo() {
  if (!paged_vmo_info_.vmo.is_valid())
    return;

  if (paged_vfs() && is_registered_with_pager_)
    paged_vfs()->UnregisterPagedVmo(paged_vmo_info_.id);

  paged_vmo_info_.vmo.reset();
  paged_vmo_info_.id = 0;
}

void PagedVnode::OnNoPagedVmoClones() {
  // It is now save to release the VMO. Since we know there are no clones, we don't have to
  // call zx_pager_detach_vmo() to stop delivery of requests.
  FreePagedVmo();
}

void PagedVnode::OnNoPagedVmoClonesMessage(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                           zx_status_t status, const zx_packet_signal_t* signal) {
  std::lock_guard lock(mutex_);

  ZX_DEBUG_ASSERT(has_clones_);

  if (!paged_vfs())
    return;  // Called during tear-down.

  // The kernel signal delivery could have raced with us creating a new clone. Validate that there
  // are still no clones before tearing down.
  zx_info_vmo_t info;
  if (paged_vmo().get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr) != ZX_OK)
    return;  // Something wrong with the VMO, don't try to tear down.
  if (info.num_children > 0) {
    // Race with new VMO. Re-arm the clone watcher and continue as if the signal was not sent.
    WatchForZeroVmoClones();
    return;
  }

  StopWatchingForZeroVmoClones();
  OnNoPagedVmoClones();
}

void PagedVnode::WatchForZeroVmoClones() {
  if (!has_clones_) {
    has_clones_ = true;

    clone_watcher_.set_object(paged_vmo().get());
    clone_watcher_.set_trigger(ZX_VMO_ZERO_CHILDREN);
    clone_watcher_.Begin(paged_vfs()->dispatcher());
  }
}

void PagedVnode::StopWatchingForZeroVmoClones() {
  if (has_clones_) {
    has_clones_ = false;
    clone_watcher_.set_object(ZX_HANDLE_INVALID);
  }
}

}  // namespace fs
