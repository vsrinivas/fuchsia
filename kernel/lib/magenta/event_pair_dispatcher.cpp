// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/event_pair_dispatcher.h>

#include <assert.h>
#include <err.h>

#include <kernel/auto_lock.h>
#include <magenta/state_tracker.h>
#include <mxalloc/new.h>

constexpr mx_rights_t kDefaultEventPairRights =
    MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER | MX_RIGHT_READ | MX_RIGHT_WRITE;

constexpr uint32_t kUserSignalMask = MX_EVENT_SIGNALED | MX_USER_SIGNAL_ALL;

status_t EventPairDispatcher::Create(mxtl::RefPtr<Dispatcher>* dispatcher0,
                                     mxtl::RefPtr<Dispatcher>* dispatcher1,
                                     mx_rights_t* rights) {
    AllocChecker ac;
    auto disp0 = new (&ac) EventPairDispatcher();
    if (!ac.check())
        return ERR_NO_MEMORY;

    auto disp1 = new (&ac) EventPairDispatcher();
    if (!ac.check()) {
        delete disp0;
        return ERR_NO_MEMORY;
    }

    *rights = kDefaultEventPairRights;
    *dispatcher0 = mxtl::AdoptRef<Dispatcher>(disp0);
    *dispatcher1 = mxtl::AdoptRef<Dispatcher>(disp1);

    disp0->Init(disp1);
    disp1->Init(disp0);

    return NO_ERROR;
}

EventPairDispatcher::~EventPairDispatcher() {}

void EventPairDispatcher::on_zero_handles() {
    canary_.Assert();

    AutoLock locker(&lock_);
    DEBUG_ASSERT(other_);

    other_->state_tracker_.InvalidateCookie(other_->get_cookie_jar());
    other_->state_tracker_.UpdateState(0u, MX_EPAIR_PEER_CLOSED);
    other_.reset();
}

status_t EventPairDispatcher::user_signal(uint32_t clear_mask, uint32_t set_mask, bool peer) {
    canary_.Assert();

    if ((set_mask & ~kUserSignalMask) || (clear_mask & ~kUserSignalMask))
        return ERR_INVALID_ARGS;

    if (!peer) {
        state_tracker_.UpdateState(clear_mask, set_mask);
        return NO_ERROR;
    }

    AutoLock locker(&lock_);
    // object_signal() may race with handle_close() on another thread.
    if (!other_)
        return ERR_PEER_CLOSED;
    other_->state_tracker_.UpdateState(clear_mask, set_mask);
    return NO_ERROR;
}

EventPairDispatcher::EventPairDispatcher()
        : state_tracker_(0u),
          other_koid_(0ull) {}

// This is called before either EventPairDispatcher is accessible from threads other than the one
// initializing the event pair, so it does not need locking.
void EventPairDispatcher::Init(EventPairDispatcher* other) TA_NO_THREAD_SAFETY_ANALYSIS {
    DEBUG_ASSERT(other);
    // No need to take |lock_| here.
    DEBUG_ASSERT(!other_);
    other_koid_ = other->get_koid();
    other_ = mxtl::RefPtr<EventPairDispatcher>(other);
}
