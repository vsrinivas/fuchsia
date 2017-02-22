// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_util/macros.h"
#include "mx/event.h"
#include "platform_semaphore.h"

namespace magma {

class MagentaPlatformSemaphore : public PlatformSemaphore {
public:
    MagentaPlatformSemaphore(mx::event event, uint64_t koid) : event_(std::move(event)), koid_(koid)
    {
    }

    uint64_t id() override { return koid_; }

    bool duplicate_handle(uint32_t* handle_out) override;

    void Reset() override { event_.signal(mx_signal(), 0); }

    void Signal() override
    {
        mx_status_t status = event_.signal(0u, mx_signal());
        DASSERT(status == NO_ERROR);
    }

    bool Wait(uint64_t timeout_ms) override;

private:
    mx_signals_t mx_signal() { return MX_EVENT_SIGNALED; }

    mx::event event_;
    uint64_t koid_;
};

} // namespace magma
