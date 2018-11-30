// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/pager_dispatcher.h>
#include <trace.h>
#include <vm/page_source.h>

#define LOCAL_TRACE 0

zx_status_t PagerDispatcher::Create(fbl::RefPtr<Dispatcher>* dispatcher, zx_rights_t* rights) {
    fbl::AllocChecker ac;
    auto disp = new (&ac) PagerDispatcher();
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    *rights = default_rights();
    *dispatcher = fbl::AdoptRef<Dispatcher>(disp);
    return ZX_OK;
}

PagerDispatcher::PagerDispatcher() : SoloDispatcher() {}

PagerDispatcher::~PagerDispatcher() {}

zx_status_t PagerDispatcher::CreateSource(fbl::RefPtr<PortDispatcher> port,
                                          uint64_t key, fbl::RefPtr<PageSource>* src_out) {
    fbl::AllocChecker ac;
    auto wrapper = fbl::make_unique_checked<PageSourceWrapper>(&ac, this, ktl::move(port), key);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    auto src = fbl::AdoptRef(new (&ac) PageSource(wrapper.get(), get_koid()));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    fbl::AutoLock lock(&wrapper->mtx_);
    wrapper->src_ = src;

    fbl::AutoLock lock2(&mtx_);
    srcs_.push_front(ktl::move(wrapper));

    *src_out = ktl::move(src);
    return ZX_OK;
}

void PagerDispatcher::ReleaseSource(PageSourceWrapper* src) {
    fbl::AutoLock lock(&mtx_);
    srcs_.erase(*src);
}

void PagerDispatcher::on_zero_handles() {
    fbl::DoublyLinkedList<fbl::unique_ptr<PageSourceWrapper>> srcs;

    mtx_.Acquire();
    while (!srcs_.is_empty()) {
        auto& src = srcs_.front();
        fbl::RefPtr<PageSource> inner;
        {
            fbl::AutoLock lock(&src.mtx_);
            inner = src.src_;
        }

        // Call close outside of the lock, since it will call back into ::OnClose.
        mtx_.Release();
        if (inner) {
            inner->Close();
        }
        mtx_.Acquire();
    }
    mtx_.Release();
}

PageSourceWrapper::PageSourceWrapper(PagerDispatcher* dispatcher,
                                     fbl::RefPtr<PortDispatcher> port, uint64_t key)
    : pager_(dispatcher), port_(ktl::move(port)), key_(key) {
    LTRACEF("%p key %lx\n", this, key_);
}

PageSourceWrapper::~PageSourceWrapper() {
    LTRACEF("%p\n", this);
    DEBUG_ASSERT(closed_);
}

void PageSourceWrapper::OnClose() {
    {
        fbl::AutoLock lock(&mtx_);
        closed_ = true;
    }
    pager_->ReleaseSource(this);
}
