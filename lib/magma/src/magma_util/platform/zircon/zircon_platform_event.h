// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_util/macros.h"
#include "platform_event.h"
#include <lib/zx/event.h>
#include <lib/zx/time.h>

namespace magma {

class ZirconPlatformEvent : public PlatformEvent {
public:
    ZirconPlatformEvent(zx::event event) : zx_event_(std::move(event)) {}

    void Signal() override
    {
        zx_status_t status = zx_event_.signal(0u, zx_signal());
        DASSERT(status == ZX_OK);
    }

    bool Wait(uint64_t timeout_ms) override
    {
        zx_signals_t pending = 0;

        zx_status_t status =
            zx_event_.wait_one(zx_signal(),
                               timeout_ms == UINT64_MAX ? zx::time::infinite()
                                                        : zx::deadline_after(zx::msec(timeout_ms)),
                               &pending);
        DASSERT(status == ZX_OK || status == ZX_ERR_TIMED_OUT);

        return pending & zx_signal();
    }

    zx_handle_t zx_handle() { return zx_event_.get(); }

    zx_signals_t zx_signal() { return ZX_EVENT_SIGNALED; }

private:
    zx::event zx_event_;
};

} // namespace magma
