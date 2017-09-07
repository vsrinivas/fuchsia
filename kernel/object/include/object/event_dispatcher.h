// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/types.h>
#include <fbl/canary.h>
#include <object/dispatcher.h>
#include <object/state_tracker.h>

#include <sys/types.h>

class EventDispatcher final : public Dispatcher {
public:
    static mx_status_t Create(uint32_t options, fbl::RefPtr<Dispatcher>* dispatcher,
                              mx_rights_t* rights);

    ~EventDispatcher() final;
    mx_obj_type_t get_type() const final { return MX_OBJ_TYPE_EVENT; }
    StateTracker* get_state_tracker() final { return &state_tracker_; }
    CookieJar* get_cookie_jar() final { return &cookie_jar_; }
    mx_status_t user_signal(uint32_t clear_mask, uint32_t set_mask, bool peer) final;

private:
    explicit EventDispatcher(uint32_t options);
    fbl::Canary<fbl::magic("EVTD")> canary_;
    StateTracker state_tracker_;
    CookieJar cookie_jar_;
};
