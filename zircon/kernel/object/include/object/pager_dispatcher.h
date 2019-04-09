// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <fbl/canary.h>
#include <fbl/ref_ptr.h>
#include <object/dispatcher.h>
#include <object/handle.h>
#include <object/port_dispatcher.h>
#include <vm/page_source.h>
#include <zircon/types.h>

// Page source implementation that talks to a userspace pager service.
//
// The lifecycle of this class is a little complicated because the pager source's port potentially
// has an unmanaged reference to the pager source through packet_. Because of this, we need to
// ensure that the last RefPtr to the pager source isn't released too early when the pager source
// gets closed. Normally, the dispatcher can retain its reference to the pager source until the
// port frees its reference to packet_ (through the PortAllocator). However, if the dispatcher is
// destroyed, if we can't revoke the port's reference to packet_, then we end up making the pager
// source keep a reference to itself until the packet is freed.
class PagerSource : public PageSource , public PortAllocator,
                    public fbl::DoublyLinkedListable<fbl::RefPtr<PagerSource>> {
private:
    PagerSource(PagerDispatcher* dispatcher, fbl::RefPtr<PortDispatcher> port, uint64_t key);
    virtual ~PagerSource();
    friend fbl::RefPtr<PagerSource>;

    PortPacket* Alloc() final {
        DEBUG_ASSERT(false);
        return nullptr;
    }
    void Free(PortPacket* port_packet) final ;

    virtual bool GetPage(uint64_t offset,
                         vm_page_t** const page_out, paddr_t* const pa_out) final {
        // Pagers cannot synchronusly fulfill requests.
        return false;
    }

    void GetPageAsync(page_request_t* request) final;
    void ClearAsyncRequest(page_request_t* request) final;
    void SwapRequest(page_request_t* old, page_request_t* new_req) final;
    void OnClose() final;
    void OnDetach() final;
    zx_status_t WaitOnEvent(event_t* event) final;

    PagerDispatcher* const pager_;
    const fbl::RefPtr<PortDispatcher> port_;
    const uint64_t key_;

    mutable DECLARE_MUTEX(PagerSource) mtx_;
    bool closed_ TA_GUARDED(mtx_) = false;
    // Flag set when there is a pending ZX_PAGER_VMO_COMPLETE message. This serves as a proxy
    // for whether or not the port has a reference to packet_ (as the complete message is the
    // last message sent). This flag is used to delay cleanup if PagerSource::Close is called
    // while the port still has a reference to packet_.
    bool complete_pending_ TA_GUARDED(mtx_) = false;
    // Ref to keep the object alive in certain circumstances - see PagerDispatcher::on_zero_handles.
    fbl::RefPtr<PagerSource> self_ref_ TA_GUARDED(mtx_);

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
        .node = LIST_INITIAL_CLEARED_VALUE, .offset = 0, .length = 0,
    };

    // Queues the page request, either sending it to the port or putting it in pending_requests_.
    void QueueMessageLocked(page_request_t* request) TA_REQ(mtx_);

    // Called when the packet becomes free. If pending_requests_ is non-empty, queues the
    // next request.
    void OnPacketFreedLocked() TA_REQ(mtx_);

    // Called by the dispatcher when it is about to go away. Handles cleaning up port's
    // reference to this object.
    void OnDispatcherClosed();

    friend PagerDispatcher;
};

class PagerDispatcher final : public SoloDispatcher<PagerDispatcher, ZX_DEFAULT_PAGER_RIGHTS> {
public:
    static zx_status_t Create(KernelHandle<PagerDispatcher>* handle, zx_rights_t* rights);
    ~PagerDispatcher() final;

    zx_status_t CreateSource(fbl::RefPtr<PortDispatcher> port,
                             uint64_t key, fbl::RefPtr<PageSource>* src);
    // Drop and return this object's reference to |src|. Must be called under
    // |src|'s lock to prevent races with dispatcher teardown.
    fbl::RefPtr<PagerSource> ReleaseSource(PagerSource* src) TA_REQ(src->mtx_);

    zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_PAGER; }

    void on_zero_handles() final;

private:
    explicit PagerDispatcher();

    mutable DECLARE_MUTEX(PagerDispatcher) list_mtx_;
    fbl::DoublyLinkedList<fbl::RefPtr<PagerSource>> srcs_ TA_GUARDED(list_mtx_);
};
