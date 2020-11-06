// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "object/event_pair_dispatcher.h"

#include <assert.h>
#include <lib/counters.h>
#include <zircon/errors.h>
#include <zircon/rights.h>
#include <zircon/types.h>

#include <fbl/alloc_checker.h>

KCOUNTER(dispatcher_eventpair_create_count, "dispatcher.eventpair.create")
KCOUNTER(dispatcher_eventpair_destroy_count, "dispatcher.eventpair.destroy")

zx_status_t EventPairDispatcher::Create(KernelHandle<EventPairDispatcher>* handle0,
                                        KernelHandle<EventPairDispatcher>* handle1,
                                        zx_rights_t* rights) {
  fbl::AllocChecker ac;
  auto holder0 = fbl::AdoptRef(new (&ac) PeerHolder<EventPairDispatcher>());
  if (!ac.check())
    return ZX_ERR_NO_MEMORY;
  auto holder1 = holder0;

  KernelHandle ep0(fbl::AdoptRef(new (&ac) EventPairDispatcher(ktl::move(holder0))));
  if (!ac.check())
    return ZX_ERR_NO_MEMORY;

  KernelHandle ep1(fbl::AdoptRef(new (&ac) EventPairDispatcher(ktl::move(holder1))));
  if (!ac.check())
    return ZX_ERR_NO_MEMORY;

  ep0.dispatcher()->Init(ep1.dispatcher());
  ep1.dispatcher()->Init(ep0.dispatcher());

  *rights = default_rights();
  *handle0 = ktl::move(ep0);
  *handle1 = ktl::move(ep1);

  return ZX_OK;
}

EventPairDispatcher::~EventPairDispatcher() { kcounter_add(dispatcher_eventpair_destroy_count, 1); }

void EventPairDispatcher::on_zero_handles_locked() { canary_.Assert(); }

void EventPairDispatcher::OnPeerZeroHandlesLocked() {
  UpdateStateLocked(0u, ZX_EVENTPAIR_PEER_CLOSED);
}

EventPairDispatcher::EventPairDispatcher(fbl::RefPtr<PeerHolder<EventPairDispatcher>> holder)
    : PeeredDispatcher(ktl::move(holder)) {
  kcounter_add(dispatcher_eventpair_create_count, 1);
}

// This is called before either EventPairDispatcher is accessible from threads other than the one
// initializing the event pair, so it does not need locking.
void EventPairDispatcher::Init(fbl::RefPtr<EventPairDispatcher> other)
    TA_NO_THREAD_SAFETY_ANALYSIS {
  DEBUG_ASSERT(other);
  // No need to take |lock_| here.
  DEBUG_ASSERT(!peer_);
  peer_koid_ = other->get_koid();
  peer_ = ktl::move(other);
}
