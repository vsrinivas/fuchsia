// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_VFS_CPP_PAGED_VNODE_H_
#define SRC_LIB_STORAGE_VFS_CPP_PAGED_VNODE_H_

#include <lib/async/cpp/wait.h>
#include <lib/async/dispatcher.h>
#include <lib/zx/port.h>
#include <lib/zx/result.h>
#include <lib/zx/vmo.h>
#include <zircon/compiler.h>

#include <cstdint>

#include <fbl/recycler.h>
#include <fbl/ref_ptr.h>

#include "src/lib/storage/vfs/cpp/paged_vfs.h"
#include "src/lib/storage/vfs/cpp/vnode.h"

namespace fs {

// A Vnode that supports paged I/O.
//
// To supply pager requests:
//
//  - Implement Vnode::GetVmo().
//     - Use PagedVnode::EnsureCreateVmo() to create the data mapping. This will create it in such a
//       way that it's registered with the paging system for callbacks.
//     - Do paged_vmo().create_child() to clone the VMO backing this node.
//     - Set the rights on the cloned VMO with the rights passed to GetVmo().
//     - Call DidCloneVmo() to start tracking the clone.
//     - Populate the GetVmo() out parameter with the child VMO.
//  - Implement VmoRead() to fill the VMO data when requested.
//  - Implement VmoDirty() to deal with the transition of the VMO page state from clean to dirty.
//
// To unregister from pager requests:
//
//  - This class will be automatically kept in scope by has_clones_reference_ as long as there are
//    memory mappings.
//  - The VMO will be automatically freed when there are no more mappings.
//     - You can override this behavior by overriding OnNoPagedVmoClones().
class PagedVnode : public Vnode, public fbl::Recyclable<PagedVnode> {
 public:
  // Required for memory management, see the class comment above Vnode for more.
  void fbl_recycle() { RecycleNode(); }

  // Called by the paging system in response to a kernel request to fill data into this node's VMO.
  //
  //  - On success, calls vfs()->SupplyPages() with the created data range.
  //  - On failure, calls vfs()->ReportPagerError() with the error information.
  //
  // The success or failure cases can happen synchronously (from within this call stack) or
  // asynchronously in the future. Failure to report success or failure will hang the requesting
  // process.
  //
  // Note that offset + length will be page-aligned so can extend beyond the end of the file.
  //
  // Race conditions
  // ---------------
  //
  // Since the OnNoPagedVmoClones() is not synchronized with page requests, these requests can come
  // in after there are no clones.
  //
  // Additionally, because this function is called outside of the Vfs' lock, this function can be
  // called even if the VMO handle is detached and freed. If the VMO handle is freed, it is safe to
  // drop this request (there's no way to even report an error without a handle). More
  // theoretically, if the VMO was freed, then a new one created, and then we get a stale read for
  // the first one, the kernel might not actually need the pages but unnecessary read won't be
  // harmful.
  //
  // If the VMO still exists and could possibly be used in the future (even if it isn't cloned now),
  // all page requests should be fulfilled to the extent possible to avoid accumulating failed state
  // in the kernel (See OnNoPagedVmoClones() for more).
  virtual void VmoRead(uint64_t offset, uint64_t length) = 0;

  // Called by the paging system in response to a kernel request to transit page state.
  //
  //  - On success, calls vfs()->DirtyPages() with the requested data range.
  //  - On failure, calls vfs()->ReportPagerError() with the error information.
  //
  // The success or failure cases can happen synchronously. Failure to report success or failure
  // will hang the requesting process.
  //
  // Note that offset + length will be page-aligned so can extend beyond the end of the file.
  //
  // Race conditions
  // ---------------
  //
  // The consideration of race condition with OnNoPagedVmoClones() is the same as VmoRead().
  //
  // Theoretically, VmoDirty() requests can be handled with freed VMO. This unnecessarily changes
  // the page state from clean to dirty. In this case, there is no problem with data integrity
  // because the data on the dirty page has not been changed.
  //
  // |mutex_| lock should be held inside VmoDirty() to avoid race condition with
  // CreatePagedNodeVmo().
  virtual void VmoDirty(uint64_t offset, uint64_t length);

  // Called when the VFS associated with this node is shutting down. The associated VFS will still
  // be valid at the time of the call.
  void WillDestroyVfs();

  // This is called by PagedVfs to tear down the node.
  void TearDown();

 protected:
  friend fbl::RefPtr<PagedVnode>;

  explicit PagedVnode(PagedVfs& vfs);

  // has_value iff the vfs hasn't been shut down.
  std::optional<std::reference_wrapper<PagedVfs>> vfs() __TA_REQUIRES_SHARED(mutex_) {
    return vfs_;
  }

  // Returns the vmo associated with the paging system, if any. This will be a null handle if there
  // is no paged vmo associated with this vnode.
  //
  // Populate with EnsureCreatePagedVmo(), free with FreeVmo().
  //
  // This vmo must not be mapped and then written to. Doing so will cause the kernel to "page in"
  // the vmo which will reenter the filesystem to populate it, which is not what you want when
  // writing to it.
  //
  // It is theoretically possible to read from this vmo (either mapped or using zx::vmo::read()) but
  // the caller must be VERY careful and it is strongly recommended that you avoid this. Reading
  // will cause the data to be paged in which will reenter the PagedVnode. Therefore, the mutex_
  // must NOT be held during the read process. The caller's memory management structure must then
  // guarantee that everything remain valid across this unlocked period (the vnode could be closed
  // on another thread) or it must be able to handle the ensuing race conditions.
  const zx::vmo& paged_vmo() const __TA_REQUIRES_SHARED(mutex_) { return paged_vmo_info_.vmo; }

  // Returns true if there are clones of the VMO alive that have been given out.
  bool has_clones() const __TA_REQUIRES_SHARED(mutex_) { return !!has_clones_reference_; }

  // Populates the paged_vmo() if necessary. Does nothing if it already exists. Access the created
  // vmo with this class' paged_vmo() getter. This will register with the PagedVfs and will start
  // serving kernel page requests.
  //
  // When a mapping is requested, the derived class should call this function, create a
  // clone of the paged_vmo_ with the desired flags, and then call DidCloneVmo().
  zx::result<> EnsureCreatePagedVmo(uint64_t size, uint32_t options = 0) __TA_REQUIRES(mutex_);

  // Call after successfully creating a paged_vmo() clone. This will ensure that the tracking
  // information for clones is set up:
  //   - The has_clones_reference_ keeps this object alive as long as there are clones.
  //   - The clone_watcher_ will notice when there are no clones, send notifications, and clean up
  //     the has_clones_reference_.
  void DidClonePagedVmo() __TA_REQUIRES(mutex_);

  // Releases the vmo_ and unregisters for paging notifications from the PagedVfs. This will
  // detach the VMO from the pager and unregister from paging notifications.
  //
  // If there are any clones of the vmo alive, all future paging requests on those vmo clones will
  // fail.
  //
  // This function returns any reference held on behalf of the pager that is keeping this class
  // alive. The caller should ensure that this reference (if non-null) is safely released outside of
  // the Vnode's mutex_.
  //
  // TODO(fxbug.dev/51111) make the return value a PagedVnode. Using the base class here allows
  // the blobfs conversion to the new pager easier.
  [[nodiscard]] fbl::RefPtr<Vnode> FreePagedVmo() __TA_REQUIRES(mutex_);

  // Implementors of this class can override this function to response to the event that there
  // are no more clones of the vmo_. The default implementation calls FreePagedVmo().
  //
  // Some implementations may want to cache the vmo object and therefore avoid calling
  // FreePagedVmo().
  //
  // Note that it is important to continue serving page requests for as long as the VMO could
  // re-used. If a race causes a page request to be delivered after the "no clones" message, failing
  // or dropping the request will accumulate that state in the kernel for the VMO which can affect
  // future clones of the VMO.
  virtual void OnNoPagedVmoClones() __TA_REQUIRES(mutex_);

 private:
  // Callback handler for the "no clones" message. Due to kernel message delivery race conditions
  // there might actually be clones. This checks and calls OnNoPagedVmoClones() when needed.
  void OnNoPagedVmoClonesMessage(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                 zx_status_t status, const zx_packet_signal_t* signal)
      __TA_EXCLUDES(mutex_);

  // Starts or stops the clone_watcher_ to observe the case of no vmo_ clones. The WaitMethod is
  // called only once per "watch" call so this needs to be re-called after triggering. These can
  // be called more than once.
  //
  // The vmo_ and paged_vfs() must exist.
  void WatchForZeroVmoClones() __TA_REQUIRES(mutex_);
  void StopWatchingForZeroVmoClones() __TA_REQUIRES(mutex_);

  // The root VMO that paging happens out of for this vnode. VMOs that map the data into user
  // processes will be children of this VMO.
  PagedVfs::VmoCreateInfo paged_vmo_info_ __TA_GUARDED(mutex_);

  // Set when there are clones of the vmo_ to keep this object in scope. Since there is no
  // connection owning a reference to this class we need to handle the memory management ourselves.
  fbl::RefPtr<PagedVnode> has_clones_reference_ __TA_GUARDED(mutex_);

  // Watches any clones of "paged_vmo()" provided to clients. Observes the ZX_VMO_ZERO_CHILDREN
  // signal. See WatchForZeroChildren().
  async::WaitMethod<PagedVnode, &PagedVnode::OnNoPagedVmoClonesMessage> clone_watcher_
      __TA_GUARDED(mutex_);

  std::optional<std::reference_wrapper<PagedVfs>> vfs_ __TA_GUARDED(mutex_);
};

}  // namespace fs

#endif  // SRC_LIB_STORAGE_VFS_CPP_PAGED_VNODE_H_
