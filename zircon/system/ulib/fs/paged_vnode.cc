// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/paged_vfs.h>
#include <fs/paged_vnode.h>

namespace fs {

PagedVnode::PagedVnode(PagedVfs* vfs) : vfs_(vfs), clone_watcher_(this) {}

PagedVnode::~PagedVnode() {}

void PagedVnode::DetachVfs() { vfs_ = nullptr; }

zx::status<> PagedVnode::EnsureCreateVmo(uint64_t size) {
  if (vmo_)
    return zx::ok();
  if (!vfs_)
    return zx::error(ZX_ERR_BAD_STATE);  // Currently shutting down.

  auto vfs_or = vfs_->CreatePagedNodeVmo(fbl::RefPtr<PagedVnode>(this), size);
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
  // TODO(fxbug.dev/51111) Needs a lock to prevent getting requests from another thread.

  if (!vfs_)
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

  OnNoClones();
}

void PagedVnode::WatchForZeroVmoClones() {
  clone_watcher_.set_object(vmo_.get());
  clone_watcher_.set_trigger(ZX_VMO_ZERO_CHILDREN);
  clone_watcher_.Begin(vfs_->dispatcher());
}

}  // namespace fs
