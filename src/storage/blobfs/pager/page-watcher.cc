// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "page-watcher.h"

#include <zircon/status.h>

#include <blobfs/format.h>
#include <fbl/auto_lock.h>
#include <fs/trace.h>

namespace blobfs {
namespace pager {

// Called from the blobfs main thread.
zx_status_t PageWatcher::CreatePagedVmo(size_t vmo_size, zx::vmo* vmo_out) {
  TRACE_DURATION("blobfs", "PageWatcher::CreatePagedVmo", "vmo_size", vmo_size);

  uint32_t vmo_options = 0;
  zx::vmo vmo;
  zx_status_t status = page_request_handler_.CreateVmo(
      user_pager_->Dispatcher(), zx::unowned_pager(user_pager_->Pager().get()), vmo_options,
      vmo_size, &vmo);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to create paged VMO: %s\n", zx_status_get_string(status));
    return status;
  }
  {
    // The call to |CreateVmo| succeeded. The VMO is now attached to the pager, so we need to
    // wait for a detach before we can destroy the PageWatcher cleanly - |vmo_attached_to_pager_|
    // tracks that and is set to false on receiving a ZX_PAGER_VMO_COMPLETE packet (on the pager
    // detach path).
    fbl::AutoLock guard(&vmo_attached_mutex_);
    vmo_attached_to_pager_ = true;
  }
  vmo_ = zx::unowned_vmo(vmo);
  *vmo_out = std::move(vmo);
  return ZX_OK;
}

// Called from the blobfs main thread.
void PageWatcher::DetachPagedVmoSync() {
  TRACE_DURATION("blobfs", "PageWatcher::DetachPagedVmoSync");

  page_request_handler_.Detach();
  // Wait on signal from the page request handler.
  fbl::AutoLock guard(&vmo_attached_mutex_);
  while (vmo_attached_to_pager_) {
    vmo_attached_condvar_.Wait(&vmo_attached_mutex_);
  }
}

// Called from the singleton userpager thread.
void PageWatcher::HandlePageRequest(async_dispatcher_t* dispatcher, async::PagedVmoBase* paged_vmo,
                                    zx_status_t status, const zx_packet_page_request_t* request) {
  TRACE_DURATION("blobfs", "PageWatcher::HandlePageRequest", "command", request->command, "offset",
                 request->offset, "length", request->length);

  // The async loop is shutting down. The VMO has been detached from the pager, mark it safe to
  // destroy.
  if (status == ZX_ERR_CANCELED) {
    // Signal here without waiting for a ZX_PAGER_VMO_COMPLETE packet, to prevent holding up
    // destruction indefinitely. The pager async loop is shutting down, so we won't receive any more
    // packets on its port.
    SignalPagerDetach();
    return;
  }
  // The only other |status| we expect is ZX_OK.
  ZX_DEBUG_ASSERT(status == ZX_OK);

  ZX_DEBUG_ASSERT(request->flags == 0);

  switch (request->command) {
    case ZX_PAGER_VMO_READ: {
      PopulateAndVerifyPagesInRange(request->offset, request->length);
      return;
    }
    case ZX_PAGER_VMO_COMPLETE: {
      SignalPagerDetach();
      return;
    }
    default:
      FS_TRACE_ERROR("blobfs: Invalid pager request on vmo %u. [%u, %zu, %zu, %u]\n", vmo_->get(),
                     request->command, request->offset, request->length, request->flags);
      return;
  }
}

// Called from the singleton userpager thread.
void PageWatcher::PopulateAndVerifyPagesInRange(uint64_t offset, uint64_t length) {
  TRACE_DURATION("blobfs", "PageWatcher::PopulateAndVerifyPagesInRange", "offset", offset, "length",
                 length);

  if (!vmo_->is_valid()) {
    FS_TRACE_ERROR("blobfs: pager VMO is not valid.\n");
    // Return without calling op_range(ZX_PAGER_OP_FAIL), since that requires a valid pager VMO
    // handle. This could potentially cause the faulting thread to hang, but there's no way for us
    // to recover gracefully from this state.
    return;
  }

  PagerErrorStatus pager_error_status;
  if (is_corrupt_) {
    pager_error_status = PagerErrorStatus::kErrBadState;
    FS_TRACE_ERROR("blobfs: Pager failed page request because blob is corrupt, error: %s\n",
                   zx_status_get_string(static_cast<zx_status_t>(pager_error_status)));
  } else {
    pager_error_status = user_pager_->TransferPagesToVmo(offset, length, *vmo_, userpager_info_);
    if (pager_error_status != PagerErrorStatus::kOK) {
      FS_TRACE_ERROR("blobfs: Pager failed to transfer pages to the blob, error: %s\n",
                     zx_status_get_string(static_cast<zx_status_t>(pager_error_status)));
    }
  }

  if (pager_error_status != PagerErrorStatus::kOK) {
    zx_status_t status = user_pager_->Pager().op_range(
        ZX_PAGER_OP_FAIL, *vmo_, offset, length, static_cast<zx_status_t>(pager_error_status));
    if (status != ZX_OK) {
      FS_TRACE_ERROR("blobfs: op_range ZX_PAGER_OP_FAIL failed with %s\n",
                     zx_status_get_string(status));
      return;
    }

    // We've signaled a failure and unblocked outstanding page requests for this range. If the pager
    // error was a verification error, fail future requests as well - we should not service further
    // page requests on a corrupt blob.
    //
    // Note that we cannot simply detach the VMO from the pager here. There might be outstanding
    // page requests which have been queued but are yet to be serviced. These need to be handled
    // correctly - if the VMO is detached, there will be no way for us to communicate failure to
    // the kernel, since zx_pager_op_range() requires a valid pager VMO handle. Without being able
    // to make a call to zx_pager_op_range() to indicate a failed page request, the faulting thread
    // would hang indefinitely.
    if (pager_error_status == PagerErrorStatus::kErrDataIntegrity) {
      is_corrupt_ = true;
    }
    return;
  }
}

// Called from the singleton userpager thread.
void PageWatcher::SignalPagerDetach() {
  TRACE_DURATION("blobfs", "PageWatcher::SignalPagerDetach");
  // Reset the mapping so that future read requests on this VMO will be ignored.
  vmo_ = zx::unowned_vmo(ZX_HANDLE_INVALID);

  // Complete the paged vmo detach. Any in-flight read requests that arrive after this will be
  // ignored.
  fbl::AutoLock guard(&vmo_attached_mutex_);
  vmo_attached_to_pager_ = false;
  vmo_attached_condvar_.Signal();
}

}  // namespace pager
}  // namespace blobfs
