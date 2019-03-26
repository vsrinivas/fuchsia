// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <zircon/rights.h>
#include <zircon/types.h>

#include <fbl/canary.h>
#include <object/dispatcher.h>
#include <object/handle.h>

#include <sys/types.h>

class EventDispatcher final :
    public SoloDispatcher<EventDispatcher, ZX_DEFAULT_EVENT_RIGHTS, ZX_EVENT_SIGNALED> {
public:
    static zx_status_t Create(uint32_t options, KernelHandle<EventDispatcher>* handle,
                              zx_rights_t* rights);

    ~EventDispatcher() final;
    zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_EVENT; }
    CookieJar* get_cookie_jar() final { return &cookie_jar_; }

private:
    explicit EventDispatcher(uint32_t options);
    CookieJar cookie_jar_;
};
