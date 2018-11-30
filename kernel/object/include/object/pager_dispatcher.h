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
class PageSourceWrapper : public PageSourceCallback,
                          public fbl::DoublyLinkedListable<fbl::unique_ptr<PageSourceWrapper>> {
public:
    PageSourceWrapper(PagerDispatcher* dispatcher, fbl::RefPtr<PortDispatcher> port, uint64_t key);
    virtual ~PageSourceWrapper();

    void OnClose() override;

private:
    PagerDispatcher* const pager_;
    const fbl::RefPtr<PortDispatcher> port_;
    const uint64_t key_;

    fbl::Mutex mtx_;
    bool closed_ TA_GUARDED(mtx_) = false;

    // The PageSource this is wrapping.
    fbl::RefPtr<PageSource> src_ TA_GUARDED(mtx_);

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
