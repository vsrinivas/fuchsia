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

  auto info_or = paged_vfs()->CreatePagedNodeVmo(this, size);
  if (info_or.is_error())
    return info_or.take_error();
  paged_vmo_info_ = std::move(info_or).value();

  return zx::ok();
}

void PagedVnode::DidClonePagedVmo() {
  // Ensure that there is an owning reference to this vnode that goes along with the VMO clones.
  // This ensures that we can continue serving page requests even if all FIDL connections are
  // closed. This reference will be released when there are no more clones.
  if (!has_clones_reference_) {
    has_clones_reference_ = fbl::RefPtr<PagedVnode>(this);

    // Watch the VMO for the presence of no children. The VMO currently has no children because we
    // just created it, but the signal will be edge-triggered.
    WatchForZeroVmoClones();
  }
}

void PagedVnode::FreePagedVmo() {
  if (!paged_vmo_info_.vmo.is_valid())
    return;

  // The paged VMO must not be freed while there are clones alive. Otherwise, paging requests for
  // these clones will hang forever. Instead, the paged vmo must be "detached" from the pager and
  // then freed.
  //
  // TODO(fxbug.dev/77019) Implement detaching properly and describe here what to do instead.
  ZX_DEBUG_ASSERT(!has_clones());

  if (paged_vfs() && paged_vmo())
    paged_vfs()->UnregisterPagedVmo(paged_vmo_info_.id);

  paged_vmo_info_.vmo.reset();
  paged_vmo_info_.id = 0;

  StopWatchingForZeroVmoClones();
}

void PagedVnode::OnNoPagedVmoClones() {
  // It is now save to release the VMO. Since we know there are no clones, we don't have to
  // call zx_pager_detach_vmo() to stop delivery of requests.
  FreePagedVmo();
}

void PagedVnode::OnNoPagedVmoClonesMessage(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                           zx_status_t status, const zx_packet_signal_t* signal) {
  // Our clone reference must be freed, but we need to do that outside of the lock.
  fbl::RefPtr<PagedVnode> clone_reference;

  {
    std::lock_guard lock(mutex_);

    ZX_DEBUG_ASSERT(has_clones());

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

    // Move our reference for releasing outside of the lock. Clearing the member will also allow the
    // OnNoPagedVmoClones() observer to see "has_clones() == false" which is the new state.
    clone_reference = std::move(has_clones_reference_);

    StopWatchingForZeroVmoClones();
    OnNoPagedVmoClones();
  }

  // Release the reference to this class. This could be the last reference keeping it alive which
  // can cause it to be freed.
  clone_reference = nullptr;

  // THIS OBJECT IS NOW POSSIBLY DELETED.
}

void PagedVnode::WatchForZeroVmoClones() {
  clone_watcher_.set_object(paged_vmo().get());
  clone_watcher_.set_trigger(ZX_VMO_ZERO_CHILDREN);
  clone_watcher_.Begin(paged_vfs()->dispatcher());
}

void PagedVnode::StopWatchingForZeroVmoClones() { clone_watcher_.set_object(ZX_HANDLE_INVALID); }

}  // namespace fs
