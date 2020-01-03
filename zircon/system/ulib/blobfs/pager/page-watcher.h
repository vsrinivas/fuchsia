// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_BLOBFS_PAGER_PAGE_WATCHER_H_
#define ZIRCON_SYSTEM_ULIB_BLOBFS_PAGER_PAGE_WATCHER_H_

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <lib/async/cpp/paged_vmo.h>
#include <lib/async/cpp/wait.h>

#include <memory>

#include <fbl/condition_variable.h>
#include <fbl/mutex.h>

#include "user-pager.h"

namespace blobfs {

// Responsible for attaching a paged VMO to a user pager, populating pages of the VMO on demand, and
// detaching the VMO from the pager when done.
class PageWatcher {
 public:
  PageWatcher(UserPager* pager, uint32_t identifier)
      : page_request_handler_(this), user_pager_(pager), identifier_(identifier) {}

  ~PageWatcher() { DetachPagedVmoSync(); }

  // Creates a paged VMO |vmo_out| that will be backed by |user_pager_|.
  // |vmo_out| is owned by the caller.
  zx_status_t CreatePagedVmo(size_t vmo_size, zx::vmo* vmo_out);

  // Sets |verifier_info_| that will be used to verify pages as they are populated.
  void SetPageVerifierInfo(std::unique_ptr<VerifierInfo> verifier_info);

  // Detaches the paged VMO from the pager and waits for the page request handler to receive a
  // ZX_PAGER_VMO_COMPLETE packet. Should be called before the associated VMO or the |PageWatcher|
  // is destroyed. This is required to prevent use-after-frees.
  //
  // TODO(rashaeqbal): Consider moving the paged VMO's |mapping_| to this class when paging is the
  // default, to directly manage the lifetime of the VMO.
  void DetachPagedVmoSync();

 private:
  // Handles a received page request port packet, which can be of two types:
  // ZX_PAGER_VMO_READ - issues reads for a certain range in the paged VMO and
  // populates the pages spanning that range.
  // ZX_PAGER_VMO_COMPLETE - acknowledges detaching of the paged VMO from the
  // user pager and prepares the page watcher for safe destruction.
  void HandlePageRequest(async_dispatcher_t* dispatcher, async::PagedVmoBase* paged_vmo,
                         zx_status_t status, const zx_packet_page_request_t* request);

  // Extends the requested read range to also include pre-fetched pages.
  void GetPrefetchRangeInBytes(const uint64_t requested_offset, const uint64_t requested_length,
                               uint64_t* prefetch_offset, uint64_t* prefetch_length);

  // Fulfills page read requests for a certain range in the paged VMO. Also verifies the range after
  // it is read in from disk. Called by |HandlePageRequest| for a ZX_PAGER_VMO_READ packet. |offset|
  // and |length| are in bytes.
  void PopulateAndVerifyPagesInRange(uint64_t offset, uint64_t length);

  // Signals condition variable that is holding up destruction, indicating that it's safe to
  // delete the paged VMO now that the pager has been detached. Called by |HandlePageRequest| on
  // a ZX_PAGER_VMO_COMPLETE packet.
  void SignalPagerDetach();

  // PagedVmoMethod instance which is responsible for creating the pager-backed VMO, handling page
  // requests on it, and detaching it when done.
  async::PagedVmoMethod<PageWatcher, &PageWatcher::HandlePageRequest> page_request_handler_;

  fbl::Mutex vmo_attached_mutex_;
  fbl::ConditionVariable vmo_attached_condvar_;
  // Used to track if the paged VMO is currently attached to the pager. If it is, the |PageWatcher|
  // cannot be destroyed. The pager can still issue requests on its |page_request_handler_| as long
  // as the VMO is attached, causing potential use-after-frees.
  bool vmo_attached_to_pager_ __TA_GUARDED(vmo_attached_mutex_) = false;

  // Pointer to the user pager. Required to create the paged VMO and populate its pages.
  UserPager* const user_pager_;

  // Unique identifier for the caller / object the |PageWatcher| is attached to. Is set at the
  // time of creation and is used by |user_pager_| to issue block reads to the underlying block
  // device.
  uint32_t const identifier_;

  // Unowned VMO corresponding to the paged VMO. Used by |page_request_handler_| to populate pages.
  zx::unowned_vmo vmo_;

  std::unique_ptr<VerifierInfo> verifier_info_ = nullptr;
};

}  // namespace blobfs

#endif  // ZIRCON_SYSTEM_ULIB_BLOBFS_PAGER_PAGE_WATCHER_H_
