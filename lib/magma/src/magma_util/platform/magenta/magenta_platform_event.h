// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_util/macros.h"
#include "mx/event.h"
#include "platform_event.h"

namespace magma {

class MagentaPlatformEvent : public PlatformEvent {
public:
    MagentaPlatformEvent(mx::event event) : mx_event_(std::move(event)) {}

    void Signal() override
    {
        mx_status_t status = mx_event_.signal(0u, mx_signal());
        DASSERT(status == NO_ERROR);
    }

    bool Wait(uint64_t timeout_ms) override
    {
        mx_signals_t pending = 0;

        mx_status_t status = mx_event_.wait_one(
            mx_signal(), timeout_ms == UINT64_MAX ? MX_TIME_INFINITE : MX_MSEC(timeout_ms),
            &pending);
        DASSERT(status == NO_ERROR || status == ERR_TIMED_OUT);

        return pending == mx_signal();
    }

    mx_handle_t mx_handle() { return mx_event_.get(); }

    mx_signals_t mx_signal() { return MX_EVENT_SIGNALED; }

private:
    mx::event mx_event_;
};

} // namespace magma
