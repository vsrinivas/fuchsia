// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <kernel/event.h>
#include <object/dispatcher.h>
#include <object/state_observer.h>

#include <zircon/types.h>
#include <fbl/canary.h>
#include <fbl/ref_ptr.h>

class Event;

class WaitStateObserver final : public StateObserver {
public:
    WaitStateObserver() : StateObserver() { }
    ~WaitStateObserver();

    // This should be called under the handle table lock. If this succeeds, End() must be called
    // (before the Event is destroyed).
    zx_status_t Begin(Event* event,
                      Handle* handle,
                      zx_signals_t watched_signals);

    // This should *not* be called under the handle table lock.
    zx_signals_t End();

private:
    WaitStateObserver(const WaitStateObserver&) = delete;
    WaitStateObserver& operator=(const WaitStateObserver&) = delete;

    // StateObserver implementation:
    Flags OnInitialize(zx_signals_t initial_state, const StateObserver::CountInfo* cinfo) final;
    Flags OnStateChange(zx_signals_t new_state) final;
    Flags OnCancel(const Handle* handle) final;

    fbl::Canary<fbl::magic("WTSO")> canary_;

    Event* event_ = nullptr;
    Handle* handle_ = nullptr;
    zx_signals_t watched_signals_ = 0u;
    zx_signals_t wakeup_reasons_;
    fbl::RefPtr<Dispatcher> dispatcher_;  // Non-null only between Begin() and End().
};
