// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_PAGER_PROXY_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_PAGER_PROXY_H_

#include <zircon/types.h>

#include <fbl/intrusive_double_list.h>
#include <fbl/ref_ptr.h>
#include <object/port_dispatcher.h>
#include <vm/page_source.h>

// Page provider implementation that talks to a userspace pager service.
//
// The lifecycle of this class is a little complicated because the pager dispatcher's port
// potentially has an unmanaged reference to the PageSource that contains the PagerProxy through
// packet_. Because of this, we need to ensure that the last RefPtr to the PageSource isn't released
// too early when the pager dispatcher gets closed. Normally, the dispatcher can retain its
// reference to the PageSource until the port frees its reference to packet_ (through the
// PortAllocator). However, if the dispatcher is destroyed, if we can't revoke the port's reference
// to packet_, then we end up making the PagerProxy keep a reference to the containing PageSource
// until the packet is freed.
class PagerProxy : public PageProvider,
                   public PortAllocator,
                   public fbl::DoublyLinkedListable<fbl::RefPtr<PagerProxy>> {
 public:
  // |options_| is a bitmask of:
  static constexpr uint32_t kTrapDirty = (1u << 0u);

  PagerProxy(PagerDispatcher* dispatcher, fbl::RefPtr<PortDispatcher> port, uint64_t key,
             uint32_t options);
  ~PagerProxy() override;

 private:
  // PortAllocator methods.
  PortPacket* Alloc() final {
    DEBUG_ASSERT(false);
    return nullptr;
  }
  void Free(PortPacket* port_packet) final;

  // PageProvider methods.
  const PageSourceProperties& properties() const final;
  void SendAsyncRequest(PageRequest* request) final;
  void ClearAsyncRequest(PageRequest* request) final;
  void SwapAsyncRequest(PageRequest* old, PageRequest* new_req) final;
  bool DebugIsPageOk(vm_page_t* page, uint64_t offset) final;
  void OnClose() final;
  void OnDetach() final;
  zx_status_t WaitOnEvent(Event* event) final;
  void Dump(uint depth) final;
  bool SupportsPageRequestType(page_request_type type) const final {
    if (type == page_request_type::READ) {
      return true;
    }
    if (type == page_request_type::DIRTY) {
      return options_ & kTrapDirty;
    }
    return false;
  }

  // Called by the pager dispatcher when it is about to go away. Handles cleaning up port's
  // reference to any in flight packets.
  void OnDispatcherClose();

  // Called by the page dispatcher to set the PageSource reference. This is guaranteed to happen
  // exactly once just after construction.
  void SetPageSourceUnchecked(fbl::RefPtr<PageSource> src);

  PagerDispatcher* const pager_;
  const fbl::RefPtr<PortDispatcher> port_;
  const uint64_t key_;

  mutable DECLARE_MUTEX(PagerProxy) mtx_;
  // Whether the page_source_ is closed, i.e. this proxy object is no longer linked to the
  // page_source_ and it can receive no more messages from the page_source_.
  bool page_source_closed_ TA_GUARDED(mtx_) = false;
  // Whether the pager_ is closed, i.e. it does not hold a reference to this proxy object anymore,
  // and might even have been destroyed. We could infer the same by setting pager_ to nullptr in
  // OnDispatcherClose, but we choose to keep pager_ as const instead.
  bool pager_dispatcher_closed_ TA_GUARDED(mtx_) = false;
  // Flag set when there is a pending ZX_PAGER_VMO_COMPLETE message. This serves as a proxy
  // for whether or not the port has a reference to packet_ (as the complete message is the
  // last message sent). This flag is used to delay cleanup if PagerProxy::Close is called
  // while the port still has a reference to packet_.
  bool complete_pending_ TA_GUARDED(mtx_) = false;

  // PortPacket used for sending all page requests to the pager service. The pager
  // dispatcher serves as packet_'s allocator. This informs the dispatcher when
  // packet_ is freed by the port, which lets the single packet be continuously reused
  // for all of the source's page requests.
  PortPacket packet_ = PortPacket(nullptr, this);
  // Bool indicating whether or not packet_ is currently queued in the port.
  bool packet_busy_ TA_GUARDED(mtx_) = false;
  // The page_request_t which corresponds to the current packet_. Can be set to nullptr if the
  // PageSource calls ClearAsyncRequest to take back the request while the packet is still busy -
  PageRequest* active_request_ TA_GUARDED(mtx_) = nullptr;
  // this can happen if ClearAsyncRequest races with a PagerProxy::Free coming from port dequeue.
  // More details about this race can be found in fxbug.dev/91935.
  // Queue of page_request_t's that have come in while packet_ is busy. The
  // head of this queue is sent to the port when packet_ is freed.
  fbl::TaggedDoublyLinkedList<PageRequest*, PageProviderTag> pending_requests_ TA_GUARDED(mtx_);

  // PageRequest used for the complete message.
  PageRequest complete_request_ TA_GUARDED(mtx_);

  // Back pointer to the PageSource that owns this instance.
  // The PageSource also has a RefPtr to this object, and so with this being a RefPtr there exists
  // a cycle. This is deliberate and allows this object to control when deletion happens to ensure
  // deletion doesn't happen whilst port packets are queued. The cycle will be explicitly cut during
  // the graceful destruction triggered by OnDispatcherClose or OnClose.
  fbl::RefPtr<PageSource> page_source_ TA_GUARDED(mtx_);

  // Queues the page request, either sending it to the port or putting it in pending_requests_.
  void QueuePacketLocked(PageRequest* request) TA_REQ(mtx_);

  // Called when the packet becomes free. If pending_requests_ is non-empty, queues the
  // next request.
  void OnPacketFreedLocked() TA_REQ(mtx_);

  // Options set at creation.
  const uint32_t options_;

  friend PagerDispatcher;
};

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_PAGER_PROXY_H_
