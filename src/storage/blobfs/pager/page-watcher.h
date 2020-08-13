// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_PAGER_PAGE_WATCHER_H_
#define SRC_STORAGE_BLOBFS_PAGER_PAGE_WATCHER_H_

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
namespace pager {

// Responsible for attaching a paged VMO to a user pager, populating pages of the VMO on demand, and
// detaching the VMO from the pager when done.
class PageWatcher {
 public:
  PageWatcher(UserPager* pager, UserPagerInfo info)
      : page_request_handler_(this), user_pager_(pager), userpager_info_(std::move(info)) {}

  ~PageWatcher() { DetachPagedVmoSync(); }

  // Creates a paged VMO |vmo_out| that will be backed by |user_pager_|.
  // |vmo_out| is owned by the caller.
  zx_status_t CreatePagedVmo(size_t vmo_size, zx::vmo* vmo_out);

  // Detaches the paged VMO from the pager and waits for the page request handler to receive a
  // ZX_PAGER_VMO_COMPLETE packet. Should be called before the associated VMO or the |PageWatcher|
  // is destroyed. This is required to prevent the use-after-free detailed in the documentation for
  // |vmo_attached_to_pager_|.
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

  // Controls access to |vmo_attached_to_pager_| from all threads.
  fbl::Mutex vmo_attached_mutex_;
  // Implements waiting on a true to false transition on |vmo_attached_to_pager_|.
  fbl::ConditionVariable vmo_attached_condvar_;
  // Used to track if the paged VMO is currently attached to the pager. If it is, the |PageWatcher|
  // cannot be destroyed. Consider a design where we did not track whether the paged VMO is attached
  // to the pager (which, in this implementation, we do). The |page_request_handler_| could then
  // dereference a destroyed |PageWatcher| as follows:
  //
  // Theoretical example of use-after-free (prevented by measures described below):
  //
  // 1. A page fault triggers _send_ of ZX_PAGER_VMO_READ to |page_request_handler_|.
  // 1. Last |close()| of a Blob on main thread triggers _send_ of ZX_PAGER_VMO_COMPLETE to
  //    |page_request_handler_|.
  // 2. Main thread owner releases |PageWatcher|, invoking |~PageWatcher()|.
  // 3. ZX_PAGER_VMO_READ _received_ before ZX_PAGER_VMO_COMPLETE, and |page_request_handler_|
  //    dereferences _already freed_ |PageWatcher| to service page fault.
  //
  // This is scenario is prevented by invoking |DetachPagedVmoSync()| inside |~PageWatcher()|,
  // forcing the main thread to wait for the ZX_PAGER_VMO_COMPLETE signal to be received by the
  // pager thread before it can free the |PageWatcher|.
  bool vmo_attached_to_pager_ __TA_GUARDED(vmo_attached_mutex_) = false;

  // Pointer to the user pager. Required to create the paged VMO and populate its pages.
  UserPager* const user_pager_;

  // Unowned VMO corresponding to the paged VMO. Used by |page_request_handler_| to populate pages.
  zx::unowned_vmo vmo_;

  // Various bits of information passed on to the user pager, not used directly by the page watcher.
  // Set at time of creation.
  UserPagerInfo userpager_info_;

  // Indicates whether the data is corrupt. Once a corruption is discovered on any portion of the
  // blob, all further page requests on the entire blob must fail.
  bool is_corrupt_ = false;
};

}  // namespace pager
}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_PAGER_PAGE_WATCHER_H_
