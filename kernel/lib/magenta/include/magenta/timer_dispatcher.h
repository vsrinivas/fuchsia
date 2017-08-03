// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/timer.h>

#include <lib/dpc.h>

#include <magenta/dispatcher.h>
#include <magenta/state_tracker.h>
#include <mxtl/canary.h>
#include <mxtl/mutex.h>

#include <sys/types.h>

class TimerDispatcher final : public Dispatcher {
public:
    static status_t Create(uint32_t options,
                           mxtl::RefPtr<Dispatcher>* dispatcher,
                           mx_rights_t* rights);

    ~TimerDispatcher() final;
    mx_obj_type_t get_type() const final { return MX_OBJ_TYPE_TIMER; }
    StateTracker* get_state_tracker() final { return &state_tracker_; }
    void on_zero_handles() final;

    // Timer specific ops.
    mx_status_t Set(mx_time_t deadline, mx_duration_t period);
    mx_status_t Cancel();

    // Timer callback.
    void OnTimerFired();

private:
    TimerDispatcher(uint32_t options);
    bool CancelTimerLocked() TA_REQ(lock_);

    mxtl::Canary<mxtl::magic("TIMR")> canary_;
    dpc_t timer_dpc_;
    mxtl::Mutex lock_;
    mx_time_t deadline_ TA_GUARDED(lock_);
    mx_duration_t period_ TA_GUARDED(lock_);
    bool cancel_pending_ TA_GUARDED(lock_);
    timer_t timer_ TA_GUARDED(lock_);
    StateTracker state_tracker_;
};
