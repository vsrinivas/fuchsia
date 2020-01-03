// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "page-watcher.h"

#include <zircon/status.h>

#include <blobfs/format.h>
#include <fbl/auto_lock.h>
#include <fs/trace.h>

namespace blobfs {

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

void PageWatcher::SetPageVerifierInfo(std::unique_ptr<VerifierInfo> verifier_info) {
  verifier_info_ = std::move(verifier_info);
}

void PageWatcher::DetachPagedVmoSync() {
  TRACE_DURATION("blobfs", "PageWatcher::DetachPagedVmoSync");

  page_request_handler_.Detach();
  // wait on signal from the page request handler
  fbl::AutoLock guard(&vmo_attached_mutex_);
  while (vmo_attached_to_pager_) {
    vmo_attached_condvar_.Wait(&vmo_attached_mutex_);
  }
}

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
      return;
  }
}

void PageWatcher::GetPrefetchRangeInBytes(const uint64_t requested_offset,
                                          const uint64_t requested_length,
                                          uint64_t* prefetch_offset, uint64_t* prefetch_length) {
  TRACE_DURATION("blobfs", "PageWatcher::GetPrefetchRangeInBytes", "requested_offset",
                 requested_offset, "requested_length", requested_length);
  // TODO(rashaeqbal): Make the cluster size dynamic once we have prefetched read efficiency
  // metrics from the kernel - what percentage of prefetched pages are actually used.
  //
  // For now read read in at least 128KB (if the blob is larger than 128KB). 128KB is completely
  // arbitrary. Tune this for optimal performance (until we can support dynamic prefetch sizing).
  constexpr uint64_t kPrefetchClusterSize = (128 * (1 << 10));

  uint64_t end_offset = requested_offset + fbl::max(kPrefetchClusterSize, requested_length);
  uint64_t total_length;
  vmo_->get_size(&total_length);
  if (end_offset > total_length) {
    end_offset = total_length;
  }

  // TODO(rashaeqbal): Consider extending the range backwards as well. Will need some way to track
  // populated ranges.
  *prefetch_offset = requested_offset;
  *prefetch_length = end_offset - *prefetch_offset;
}

// TODO(rashaeqbal): fxb/40207
// Propagate errors better. Right now we simply return after an FS_TRACE_ERROR.
// Ideally we want to signal the waiting event associated with the corresponding page request, but
// we don't have access to that here. We probably need a syscall to signal failure so the thread
// waiting on the page request can handle it. With the current design, a failure will cause it to
// block indefinitely waiting on the page request event. The only two spots this event is signaled
// are:
//   1. when the |supply_pages| syscall succeeds (look at PageSource::OnPagesSupplied()).
//   2. when the page source is detached from the VMO.
void PageWatcher::PopulateAndVerifyPagesInRange(uint64_t offset, uint64_t length) {
  TRACE_DURATION("blobfs", "PageWatcher::PopulateAndVerifyPagesInRange", "offset", offset, "length",
                 length);

  if (!vmo_->is_valid()) {
    FS_TRACE_ERROR("blobfs pager: VMO is not valid.\n");
    return;
  }

  uint64_t end;
  if (add_overflow(offset, length, &end)) {
    FS_TRACE_ERROR("blobfs pager: Invalid range, addition overflow.\n");
    return;
  }

  // Extend the range being read in to also speculatively prefetch pages.
  uint64_t prefetch_offset, prefetch_length;
  GetPrefetchRangeInBytes(offset, length, &prefetch_offset, &prefetch_length);

  if (!verifier_info_) {
    FS_TRACE_ERROR("blobfs pager: Page verifier not set.\n");
    return;
  }
  zx_status_t status = user_pager_->TransferPagesToVmo(
      identifier_, prefetch_offset, prefetch_length, *vmo_, verifier_info_.get());
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs pager: Failed to transfer pages to the blob, error: %s\n",
                   zx_status_get_string(status));
    return;
  }
}

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

}  // namespace blobfs
