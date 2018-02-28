// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <zircon/types.h>
#include <fbl/canary.h>
#include <object/dispatcher.h>

#include <sys/types.h>

class EventDispatcher final : public SoloDispatcher {
public:
    static zx_status_t Create(uint32_t options, fbl::RefPtr<Dispatcher>* dispatcher,
                              zx_rights_t* rights);

    ~EventDispatcher() final;
    zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_EVENT; }
    bool has_state_tracker() const final { return true; }
    CookieJar* get_cookie_jar() final { return &cookie_jar_; }

    zx_signals_t allowed_user_signals() const final { return ZX_USER_SIGNAL_ALL | ZX_EVENT_SIGNALED; }

private:
    explicit EventDispatcher(uint32_t options);
    fbl::Canary<fbl::magic("EVTD")> canary_;
    CookieJar cookie_jar_;
};
