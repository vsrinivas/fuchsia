// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/mutex.h>
#include <kernel/timer.h>

#include <lib/dpc.h>

#include <magenta/dispatcher.h>
#include <magenta/state_tracker.h>
#include <mxtl/canary.h>

#include <sys/types.h>

class TimerDispatcher final : public Dispatcher {
public:
    static status_t Create(uint32_t options,
                           mxtl::RefPtr<Dispatcher>* dispatcher,
                           mx_rights_t* rights);

    ~TimerDispatcher() final;
    mx_obj_type_t get_type() const final { return MX_OBJ_TYPE_TIMER; }
    StateTracker* get_state_tracker() final { return &state_tracker_; }

    // Timer specific ops.
    mx_status_t SetOneShot(lk_time_t deadline);
    mx_status_t CancelOneShot();
    void OnTimerFired();

private:
    TimerDispatcher(uint32_t options);

    mxtl::Canary<mxtl::magic("TIMR")> canary_;
    dpc_t timer_dpc_;
    Mutex lock_;
    bool active_ TA_GUARDED(lock_);
    timer_t timer_ TA_GUARDED(lock_);
    StateTracker state_tracker_;
};
