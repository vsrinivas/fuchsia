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
class PagerProxy : public PageProvider, public PortAllocator {
 public:
  PagerProxy(PagerDispatcher* dispatcher, fbl::RefPtr<PortDispatcher> port, uint64_t key);
  ~PagerProxy() override;

 private:
  // PortAllocator methods.
  PortPacket* Alloc() final {
    DEBUG_ASSERT(false);
    return nullptr;
  }
  void Free(PortPacket* port_packet) final;

  // PageProvider methods.
  bool GetPageSync(uint64_t offset, VmoDebugInfo vmo_debug_info, vm_page_t** const page_out,
                   paddr_t* const pa_out) final {
    // Pagers cannot synchronusly fulfill requests.
    return false;
  }
  void GetPageAsync(page_request_t* request) final;
  void ClearAsyncRequest(page_request_t* request) final;
  void SwapRequest(page_request_t* old, page_request_t* new_req) final;
  void OnClose() final;
  void OnDetach() final;
  zx_status_t WaitOnEvent(Event* event) final;
  // Called by the pager dispatcher when it is about to go away. Handles cleaning up port's
  // reference to the containing PageSource object.
  void OnDispatcherClose() final;
  void Dump() final;

  PagerDispatcher* const pager_;
  const fbl::RefPtr<PortDispatcher> port_;
  const uint64_t key_;

  mutable DECLARE_MUTEX(PagerProxy) mtx_;
  bool closed_ TA_GUARDED(mtx_) = false;
  // Flag set when there is a pending ZX_PAGER_VMO_COMPLETE message. This serves as a proxy
  // for whether or not the port has a reference to packet_ (as the complete message is the
  // last message sent). This flag is used to delay cleanup if PagerProxy::Close is called
  // while the port still has a reference to packet_.
  bool complete_pending_ TA_GUARDED(mtx_) = false;
  // Ref to keep the object alive in certain circumstances - see PagerDispatcher::on_zero_handles.
  fbl::RefPtr<PageSource> self_src_ref_ TA_GUARDED(mtx_);

  // PortPacket used for sending all page requests to the pager service. The pager
  // dispatcher serves as packet_'s allocator. This informs the dispatcher when
  // packet_ is freed by the port, which lets the single packet be continuously reused
  // for all of the source's page requests.
  PortPacket packet_ = PortPacket(nullptr, this);
  // Bool indicating whether or not packet_ is currently queued in the port.
  bool packet_busy_ TA_GUARDED(mtx_) = false;
  // The page_request_t which corresponds to the current packet_.
  page_request_t* active_request_ TA_GUARDED(mtx_) = nullptr;
  // Queue of page_request_t's that have come in while packet_ is busy. The
  // head of this queue is sent to the port when packet_ is freed.
  list_node_t pending_requests_ TA_GUARDED(mtx_) = LIST_INITIAL_VALUE(pending_requests_);

  // page_request_t struct used for the complete message.
  page_request_t complete_request_ TA_GUARDED(mtx_) = {
      .offset = 0,
      .length = 0,
      .pages_available_cb = nullptr,
      .drop_ref_cb = nullptr,
      .cb_ctx = nullptr,
      .provider_node = LIST_INITIAL_CLEARED_VALUE,
  };

  // Back pointer to the PageSource that owns this instance.
  PageSource* page_source_ = nullptr;

  // Queues the page request, either sending it to the port or putting it in pending_requests_.
  void QueuePacketLocked(page_request_t* request) TA_REQ(mtx_);

  // Called when the packet becomes free. If pending_requests_ is non-empty, queues the
  // next request.
  void OnPacketFreedLocked() TA_REQ(mtx_);

  friend PagerDispatcher;
};

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_PAGER_PROXY_H_
