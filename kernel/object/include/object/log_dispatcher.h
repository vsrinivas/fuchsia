// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <lib/debuglog.h>
#include <object/dispatcher.h>

#include <zircon/types.h>
#include <fbl/canary.h>
#include <fbl/mutex.h>

class LogDispatcher final : public SoloDispatcher {
public:
    static zx_status_t Create(uint32_t flags, fbl::RefPtr<Dispatcher>* dispatcher,
                              zx_rights_t* rights);

    ~LogDispatcher() final;
    zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_LOG; }
    bool has_state_tracker() const final { return true; }

    zx_status_t Write(uint32_t flags, const void* ptr, size_t len);
    zx_status_t Read(uint32_t flags, void* ptr, size_t len, size_t* actual);

private:
    explicit LogDispatcher(uint32_t flags);

    static void Notify(void* cookie);
    void Signal();

    fbl::Canary<fbl::magic("LOGD")> canary_;

    dlog_reader reader_ TA_GUARDED(get_lock());
    const uint32_t flags_;
};
