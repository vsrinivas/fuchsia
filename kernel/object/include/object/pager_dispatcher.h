// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <fbl/canary.h>
#include <fbl/ref_ptr.h>
#include <object/dispatcher.h>
#include <object/port_dispatcher.h>
#include <zircon/types.h>
#include <vm/page_source.h>

// Wrapper which maintains the object layer state of a PageSource.
class PageSourceWrapper : public PageSourceCallback, public PortAllocator,
                          public fbl::DoublyLinkedListable<fbl::unique_ptr<PageSourceWrapper>> {
public:
    PageSourceWrapper(PagerDispatcher* dispatcher, fbl::RefPtr<PortDispatcher> port, uint64_t key);
    virtual ~PageSourceWrapper();

    PortPacket* Alloc() override {
        DEBUG_ASSERT(false);
        return nullptr;
    }
    void Free(PortPacket* port_packet) override;

    virtual bool GetPage(uint64_t offset,
                         vm_page_t** const page_out, paddr_t* const pa_out) override {
        // Pagers cannot synchronusly fulfill requests.
        return false;
    }

    void GetPageAsync(page_request_t* request) override;
    void ClearAsyncRequest(page_request_t* request) override;
    void SwapRequest(page_request_t* old, page_request_t* new_req) override;
    void OnClose() override;
    zx_status_t WaitOnEvent(event_t* event) override;

private:
    PagerDispatcher* const pager_;
    const fbl::RefPtr<PortDispatcher> port_;
    const uint64_t key_;

    fbl::Mutex mtx_;
    bool closed_ TA_GUARDED(mtx_) = false;

    // The PageSource this is wrapping.
    fbl::RefPtr<PageSource> src_ TA_GUARDED(mtx_);

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

    // Queues the page request, either sending it to the port or putting it in pending_requests_.
    void QueueMessageLocked(page_request_t* request) TA_REQ(mtx_);

    // Called when the packet becomes free. If pending_requests_ is non-empty, queues the
    // next request.
    void OnPacketFreedLocked() TA_REQ(mtx_);

    friend PagerDispatcher;
};

class PagerDispatcher final : public SoloDispatcher<PagerDispatcher, ZX_DEFAULT_PAGER_RIGHTS> {
public:
    static zx_status_t Create(fbl::RefPtr<Dispatcher>* dispatcher, zx_rights_t* rights);
    ~PagerDispatcher() final;

    zx_status_t CreateSource(fbl::RefPtr<PortDispatcher> port,
                             uint64_t key, fbl::RefPtr<PageSource>* src);
    void ReleaseSource(PageSourceWrapper* src);

    zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_PAGER; }

    void on_zero_handles() final;

private:
    explicit PagerDispatcher();

    fbl::Canary<fbl::magic("PGRD")> canary_;

    fbl::Mutex mtx_;
    fbl::DoublyLinkedList<fbl::unique_ptr<PageSourceWrapper>> srcs_;
};
