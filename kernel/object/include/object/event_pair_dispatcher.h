// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/types.h>
#include <fbl/canary.h>
#include <fbl/mutex.h>
#include <fbl/ref_ptr.h>
#include <object/dispatcher.h>
#include <object/state_tracker.h>
#include <sys/types.h>

class EventPairDispatcher final : public Dispatcher {
public:
    static mx_status_t Create(fbl::RefPtr<Dispatcher>* dispatcher0,
                              fbl::RefPtr<Dispatcher>* dispatcher1,
                              mx_rights_t* rights);

    ~EventPairDispatcher() final;
    mx_obj_type_t get_type() const final { return MX_OBJ_TYPE_EVENT_PAIR; }
    StateTracker* get_state_tracker() final { return &state_tracker_; }
    CookieJar* get_cookie_jar() final { return &cookie_jar_; }
    mx_status_t user_signal(uint32_t clear_mask, uint32_t set_mask, bool peer) final;
    void on_zero_handles() final;
    mx_koid_t get_related_koid() const final { return other_koid_; }

private:
    EventPairDispatcher();
    void Init(fbl::RefPtr<EventPairDispatcher> other);

    CookieJar cookie_jar_;

    fbl::Canary<fbl::magic("EVPD")> canary_;

    StateTracker state_tracker_;

    // Set in Init(); never changes otherwise.
    mx_koid_t other_koid_;

    // Protects |other_| (except in Init(), where it's not needed).
    fbl::Mutex lock_;
    fbl::RefPtr<EventPairDispatcher> other_ TA_GUARDED(lock_);
};
