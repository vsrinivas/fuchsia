// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <magenta/dispatcher.h>
#include <magenta/state_observer.h>
#include <magenta/types.h>

#include <mxtl/ref_ptr.h>

class WaitEvent;

class WaitStateObserver final : public StateObserver {
public:
    WaitStateObserver() : StateObserver() { }
    ~WaitStateObserver();

    // This should be called under the handle table lock. If this succeeds, End() must be called
    // (before the WaitEvent is destroyed).
    mx_status_t Begin(WaitEvent* event,
                      Handle* handle,
                      mx_signals_t watched_signals);

    // This should *not* be called under the handle table lock.
    mx_signals_t End();

private:
    WaitStateObserver(const WaitStateObserver&) = delete;
    WaitStateObserver& operator=(const WaitStateObserver&) = delete;

    // StateObserver implementation:
    bool OnInitialize(mx_signals_t initial_state, const StateObserver::CountInfo* cinfo) final;
    bool OnStateChange(mx_signals_t new_state) final;
    bool OnCancel(Handle* handle) final;

    WaitEvent* event_ = nullptr;
    Handle* handle_ = nullptr;
    mx_signals_t watched_signals_ = 0u;
    mx_signals_t wakeup_reasons_;
    mxtl::RefPtr<Dispatcher> dispatcher_;  // Non-null only between Begin() and End().
};
