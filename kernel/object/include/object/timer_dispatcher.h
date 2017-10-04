// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/timer.h>
#include <lib/dpc.h>
#include <zircon/types.h>
#include <fbl/canary.h>
#include <fbl/mutex.h>
#include <object/dispatcher.h>

#include <sys/types.h>

class TimerDispatcher final : public Dispatcher {
public:
    static zx_status_t Create(uint32_t options,
                              fbl::RefPtr<Dispatcher>* dispatcher,
                              zx_rights_t* rights);

    ~TimerDispatcher() final;
    zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_TIMER; }
    bool has_state_tracker() const final { return true; }
    void on_zero_handles() final;

    // Timer specific ops.
    zx_status_t Set(zx_time_t deadline, zx_duration_t slack);
    zx_status_t Cancel();

    // Timer callback.
    void OnTimerFired();

private:
    explicit TimerDispatcher(slack_mode slack_mode);
    void SetTimerLocked(bool cancel_first) TA_REQ(lock_);
    bool CancelTimerLocked() TA_REQ(lock_);

    fbl::Canary<fbl::magic("TIMR")> canary_;
    const slack_mode slack_mode_;
    dpc_t timer_dpc_;
    fbl::Mutex lock_;
    zx_time_t deadline_ TA_GUARDED(lock_);
    zx_duration_t slack_ TA_GUARDED(lock_);
    bool cancel_pending_ TA_GUARDED(lock_);
    timer_t timer_ TA_GUARDED(lock_);
};
