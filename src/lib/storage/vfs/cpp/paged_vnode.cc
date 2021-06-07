// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/paged_vnode.h"

#include <lib/async/task.h>

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

  if (paged_vfs() && paged_vmo())
    paged_vfs()->FreePagedVmo(std::move(paged_vmo_info_));

  // Reset to known-state after moving (or in case the paged_vfs was destroyed and we skipped
  // moving out of it).
  paged_vmo_info_.vmo = zx::vmo();
  paged_vmo_info_.id = 0;

  StopWatchingForZeroVmoClones();

  if (has_clones()) {
    // In the common case, OnNoPagedVmoClonesMessage() will call OnNoPagedVmoClones() which will
    // optionally call this function to release the vmo. The has_clones_reference_ will be released
    // at the end of OnNoPagedVmoClones(), allowing this class to go out of scope if there is no
    // other use for it. In this case, has_clones() will be false before this call and this block
    // will be skipped.
    //
    // But it is possible for a node to want to free its VMO at other times, even when there are
    // clones. This might happen during filesystem tear-down, for example. In this case we need to
    // force ourselves into the "no clones" state but we can't release the has_clones_reference_ from
    // this stack since it will free our object (and the lock the caller must currently be holding
    // for us) out from under us. Instead, send the reference to the message loop to release it
    // non-reentrantly.
    async::PostTask(paged_vfs()->dispatcher(), [this_ref = std::move(has_clones_reference_)]() {});
  }
}

void PagedVnode::OnNoPagedVmoClones() {
  ZX_DEBUG_ASSERT(!has_clones());

  // It is now save to release the VMO. Since we know there are no clones, we don't have to
  // call zx_pager_detach_vmo() to stop delivery of requests.
  FreePagedVmo();
}

void PagedVnode::OnNoPagedVmoClonesMessage(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                           zx_status_t status, const zx_packet_signal_t* signal) {
  // The system will cancel our wait on teardown if we're still watching the vmo.
  if (status == ZX_ERR_CANCELED)
    return;

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
