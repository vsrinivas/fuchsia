// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magenta_platform_semaphore.h"
#include "magenta_platform_port.h"
#include "magma_util/macros.h"
#include "platform_object.h"

#include <mx/time.h>

namespace magma {

bool MagentaPlatformSemaphore::duplicate_handle(uint32_t* handle_out)
{
    mx::event duplicate;
    mx_status_t status = event_.duplicate(MX_RIGHT_SAME_RIGHTS, &duplicate);
    if (status < 0)
        return DRETF(false, "mx_handle_duplicate failed: %d", status);
    *handle_out = duplicate.release();
    return true;
}

bool MagentaPlatformSemaphore::Wait(uint64_t timeout_ms)
{
    mx_signals_t pending = 0;
    mx_status_t status = event_.wait_one(
        mx_signal(), timeout_ms == UINT64_MAX ? MX_TIME_INFINITE : mx::deadline_after(MX_MSEC(timeout_ms)), &pending);
    if (status == MX_ERR_TIMED_OUT)
        return false;

    DASSERT(status == MX_OK);
    DASSERT(pending & mx_signal());

    Reset();
    return true;
}

bool MagentaPlatformSemaphore::WaitAsync(PlatformPort* platform_port)
{
    auto port = static_cast<MagentaPlatformPort*>(platform_port);
    mx_status_t status = event_.wait_async(port->mx_port(), id(), mx_signal(), MX_WAIT_ASYNC_ONCE);
    if (status != MX_OK)
        return DRETF(false, "wait_async failed: %d", status);
    return true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<PlatformSemaphore> PlatformSemaphore::Create()
{
    mx::event event;
    mx_status_t status = mx::event::create(0, &event);
    if (status != MX_OK)
        return DRETP(nullptr, "event::create failed: %d", status);

    uint64_t koid;
    if (!PlatformObject::IdFromHandle(event.get(), &koid))
        return DRETP(nullptr, "couldn't get koid from handle");

    return std::make_unique<MagentaPlatformSemaphore>(std::move(event), koid);
}

std::unique_ptr<PlatformSemaphore> PlatformSemaphore::Import(uint32_t handle)
{
    mx::event event(handle);

    uint64_t koid;
    if (!PlatformObject::IdFromHandle(event.get(), &koid))
        return DRETP(nullptr, "couldn't get koid from handle");

    return std::make_unique<MagentaPlatformSemaphore>(std::move(event), koid);
}

} // namespace magma
