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
        mx_status_t status = mx_event_.signal(0u, MX_USER_SIGNAL_0);
        DASSERT(status == NO_ERROR);
    }

    void Wait() override
    {
        mx_status_t status = mx_event_.wait_one(MX_USER_SIGNAL_0, MX_TIME_INFINITE, nullptr);
        DASSERT(status == NO_ERROR);
    }

private:
    mx::event mx_event_;
};

std::unique_ptr<PlatformEvent> PlatformEvent::Create()
{
    mx::event event;
    mx_status_t status = mx::event::create(0, &event);
    if (status != NO_ERROR)
        return DRETP(nullptr, "event::create failed: %d", status);

    return std::make_unique<MagentaPlatformEvent>(std::move(event));
}

} // namespace magma
