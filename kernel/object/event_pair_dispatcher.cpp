// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/event_pair_dispatcher.h>

#include <assert.h>
#include <err.h>

#include <zircon/rights.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>

zx_status_t EventPairDispatcher::Create(fbl::RefPtr<Dispatcher>* dispatcher0,
                                        fbl::RefPtr<Dispatcher>* dispatcher1,
                                        zx_rights_t* rights) {
    fbl::AllocChecker ac;
    auto holder0 = fbl::AdoptRef(new (&ac) PeerHolder<EventPairDispatcher>());
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;
    auto holder1 = holder0;

    auto disp0 = fbl::AdoptRef(new (&ac) EventPairDispatcher(fbl::move(holder0)));
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    auto disp1 = fbl::AdoptRef(new (&ac) EventPairDispatcher(fbl::move(holder1)));
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    disp0->Init(disp1);
    disp1->Init(disp0);

    *rights = ZX_DEFAULT_EVENT_PAIR_RIGHTS;
    *dispatcher0 = fbl::move(disp0);
    *dispatcher1 = fbl::move(disp1);

    return ZX_OK;
}

EventPairDispatcher::~EventPairDispatcher() {}

void EventPairDispatcher::on_zero_handles()
    TA_NO_THREAD_SAFETY_ANALYSIS {
    canary_.Assert();

    fbl::AutoLock locker(get_lock());
    DEBUG_ASSERT(peer_);

    peer_->InvalidateCookieLocked(peer_->get_cookie_jar());
    peer_->UpdateStateLocked(0u, ZX_EPAIR_PEER_CLOSED);
    peer_.reset();
}

EventPairDispatcher::EventPairDispatcher(fbl::RefPtr<PeerHolder<EventPairDispatcher>> holder)
    : PeeredDispatcher(fbl::move(holder))
{}

// This is called before either EventPairDispatcher is accessible from threads other than the one
// initializing the event pair, so it does not need locking.
void EventPairDispatcher::Init(fbl::RefPtr<EventPairDispatcher> other) TA_NO_THREAD_SAFETY_ANALYSIS {
    DEBUG_ASSERT(other);
    // No need to take |lock_| here.
    DEBUG_ASSERT(!peer_);
    peer_koid_ = other->get_koid();
    peer_ = fbl::move(other);
}
