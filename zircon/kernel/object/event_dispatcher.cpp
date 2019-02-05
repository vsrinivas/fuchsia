// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/event_dispatcher.h>

#include <err.h>

#include <fbl/alloc_checker.h>
#include <lib/counters.h>
#include <zircon/rights.h>

KCOUNTER(dispatcher_event_create_count, "dispatcher.event.create");
KCOUNTER(dispatcher_event_destroy_count, "dispatcher.event.destroy");

zx_status_t EventDispatcher::Create(uint32_t options, fbl::RefPtr<Dispatcher>* dispatcher,
                                    zx_rights_t* rights) {
    fbl::AllocChecker ac;
    auto disp = new (&ac) EventDispatcher(options);
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    *rights = default_rights();
    *dispatcher = fbl::AdoptRef<Dispatcher>(disp);
    return ZX_OK;
}

EventDispatcher::EventDispatcher(uint32_t options) {
    kcounter_add(dispatcher_event_create_count, 1);
}

EventDispatcher::~EventDispatcher() {
    kcounter_add(dispatcher_event_destroy_count, 1);
}
