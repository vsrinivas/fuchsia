// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/paged_vnode.h"

#include <lib/async/task.h>
#include <zircon/errors.h>

#include <memory>
#include <mutex>

#include "src/lib/storage/vfs/cpp/paged_vfs.h"

namespace fs {

PagedVnode::PagedVnode(PagedVfs& vfs) : clone_watcher_(this), vfs_(vfs) {}

void PagedVnode::VmoDirty(uint64_t offset, uint64_t length) {
  ZX_ASSERT_MSG(false, "Filesystem does not support VmoDirty() (maybe read-only filesystem).");
}

zx::result<> PagedVnode::EnsureCreatePagedVmo(uint64_t size, uint32_t options) {
  if (paged_vmo_info_.vmo.is_valid()) {
    return zx::ok();
  }
  if (!vfs_.has_value()) {
    return zx::error(ZX_ERR_BAD_STATE);  // Currently shutting down.
  }

  zx::result info_or = vfs_.value().get().CreatePagedNodeVmo(this, size, options);
  if (info_or.is_error()) {
    return info_or.take_error();
  }
  paged_vmo_info_ = std::move(info_or.value());

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

fbl::RefPtr<Vnode> PagedVnode::FreePagedVmo() {
  if (!paged_vmo_info_.vmo.is_valid())
    return nullptr;

  // Need to stop watching before deleting the VMO or there will be no handle to stop watching.
  StopWatchingForZeroVmoClones();

  if (vfs_.has_value()) {
    vfs_.value().get().FreePagedVmo(std::move(paged_vmo_info_));
  }

  // Reset to known-state after moving (or in case the paged_vfs was destroyed and we skipped
  // moving out of it).
  paged_vmo_info_.vmo = zx::vmo();
  paged_vmo_info_.id = 0;

  // This function must not free itself since the lock must be held to call it and the caller can't
  // release a deleted lock. The has_clones_reference_ may be the last thing keeping this class
  // alive so return it to allow the caller to release it properly.
  return std::move(has_clones_reference_);
}

void PagedVnode::OnNoPagedVmoClones() {
  ZX_DEBUG_ASSERT(!has_clones());

  // It is now save to release the VMO. Since we know there are no clones, we don't have to
  // call zx_pager_detach_vmo() to stop delivery of requests. And since there are no clones, the
  // has_clones_reference_ should also be null and there shouldn't be a reference to release
  // returned by FreePagedVmo(). If there is, deleting it here would cause "this" to be deleted
  // inside its own lock which will crash.
  fbl::RefPtr<fs::Vnode> pager_reference = FreePagedVmo();
  ZX_DEBUG_ASSERT(!pager_reference);
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

    if (!vfs_.has_value()) {
      return;  // Called during tear-down.
    }

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
  if (vfs_.has_value()) {
    clone_watcher_.Begin(vfs_.value().get().dispatcher());
  }
}

void PagedVnode::StopWatchingForZeroVmoClones() {
  // This needs to tolerate calls where the cancel is unnecessary.
  if (clone_watcher_.is_pending())
    clone_watcher_.Cancel();
  clone_watcher_.set_object(ZX_HANDLE_INVALID);
}

void PagedVnode::WillDestroyVfs() {
  std::lock_guard lock(mutex_);
  vfs_.reset();
}

void PagedVnode::TearDown() {
  std::lock_guard lock(mutex_);
  auto node = FreePagedVmo();
}

}  // namespace fs
