// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_VFS_CPP_PAGED_VNODE_H_
#define SRC_LIB_STORAGE_VFS_CPP_PAGED_VNODE_H_

#include <lib/async/cpp/wait.h>
#include <zircon/compiler.h>

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
//     - Do vmo().create_child() to clone the VMO backing this node.
//     - Set the rights on the cloned VMO with the rights passed to GetVmo().
//     - Call EnsurePagedVmoRegistered() to route paging requests to this class.
//     - Populate the GetVmo() out parameter with the child VMO.
//  - Implement VmoRead() to fill the VMO data when requested.
//
// To unregister from pager requests:
//
//  - This class will be automatically kept in scope as long as there are memory mappings, and
//    this reference and the VMO will be automatically freed when there are no more mappings.
//  - You can override this behavior by overriding OnNoPagedVmoClones().
//     - Always call EnsurePagedVmoUnregistered() to free the reference forcing this class alive.
//     - Cache the VMO as desired, freeing with FreePagedVmo()
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
  // Since pager registration and unregistration is not synchronized between this class and the
  // PagedVfs, this function can be called after the VMO has been unregistered and possibly
  // destroyed. The implementation should be prepared to fail in these cases.
  virtual void VmoRead(uint64_t offset, uint64_t length) = 0;

 protected:
  friend fbl::RefPtr<PagedVnode>;

  explicit PagedVnode(PagedVfs* vfs);

  ~PagedVnode() override;

  // This will be null if the Vfs has shut down. Since Vnodes are refcounted, it's possible for them
  // to outlive their associated Vfs. Always null check before using. If there is no Vfs associated
  // with this object, all operations are expected to fail.
  PagedVfs* paged_vfs() __TA_REQUIRES_SHARED(mutex_) {
    // Since we were constructed with a PagedVfs, we know it's safe to up-cast back to that.
    return static_cast<PagedVfs*>(vfs());
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
  bool has_clones() const __TA_REQUIRES_SHARED(mutex_) { return has_clones_; }

  // Populates the paged_vmo() if necessary. Does nothing if it already exists. Access the created
  // vmo with this class' paged_vmo() getter.
  //
  // When a mapping is requested, the derived class should call this function, create a
  // clone of this VMO with the desired flags, and then call EnsurePagedVmoRegistered() to register
  // for callbacks and watch for "no clones" messages.
  zx::status<> EnsureCreatePagedVmo(uint64_t size) __TA_REQUIRES(mutex_);

  // Ensure the paged VMO is registered with the PagedVfs after a clone of the paged vmo is
  // created. These will do nothing if the desired state is already present or if the VFS is torn
  // down, but registration requires the paged vmo exists -- see EnsureCreateVmo().
  //
  // Registration will route pager requests to this node from the PagedVfs and also start watching
  // for the "no clones" notification so the node knows when to unregister. The PagedVfs will
  // hold a reference to this object when it is registered to keep it alive on behalf of the
  // kernel and users of the mappings.
  //
  // Unregistering a node can cause it to be freed if the pager reference is the only thing keeping
  // this node alive. This is bad enough, but since the lock is required to be held during this
  // call, the subsequent unlock in the calling frame will be on freed memory. Because this is
  // very difficult to implement properly, the PagedVfs will post its owning reference to the
  // message loop to ensure the Vnode is freed outside of this call stack.
  void EnsurePagedVmoRegistered() __TA_REQUIRES(mutex_);
  void EnsurePagedVmoUnregistered() __TA_REQUIRES(mutex_);

  // Releases the vmo_ and unregisters for paging notifications from the PagedVfs. If this causes
  // the Vnode to be freed, it will be destructed from a subsequent run of the message loop (see
  // EnsurePagedVmoUnregistered() above).
  void FreePagedVmo() __TA_REQUIRES(mutex_);

  // Implementors of this class can override this function to response to the event that there
  // are no more clones of the vmo_. The default implementation calls FreePagedVmo().
  //
  // Some implementations may want to cache the vmo object so avoid calling FreePagedVmo(), but
  // they will all want to unregister via EnsurePagedVmoUnregistered() because that will release
  // the reference to this class held by the PagedVfs which can cause this class to leak.
  // Unlike re-creating the vmo, registration is very lightweight so there is no consideration to
  // cache it.
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

  // Set when this class is registered with the PagedVfs for pager requests.
  bool is_registered_with_pager_ __TA_GUARDED(mutex_) = false;

  // Set when there are clones of the vmo_.
  bool has_clones_ __TA_GUARDED(mutex_) = false;

  // Watches any clones of "paged_vmo()" provided to clients. Observes the ZX_VMO_ZERO_CHILDREN
  // signal. See WatchForZeroChildren().
  async::WaitMethod<PagedVnode, &PagedVnode::OnNoPagedVmoClonesMessage> clone_watcher_
      __TA_GUARDED(mutex_);
};

}  // namespace fs

#endif  // SRC_LIB_STORAGE_VFS_CPP_PAGED_VNODE_H_
