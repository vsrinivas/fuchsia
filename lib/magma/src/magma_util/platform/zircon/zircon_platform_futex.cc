// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform_futex.h"
#include "zircon/syscalls.h"

namespace magma {

static_assert(sizeof(zx_futex_t) == sizeof(uint32_t), "futex type incompatible size");

bool PlatformFutex::Wake(uint32_t* value_ptr, int32_t wake_count)
{
    zx_status_t status;
    if ((status = zx_futex_wake(reinterpret_cast<zx_futex_t*>(value_ptr), wake_count)) != ZX_OK)
        return DRETF(false, "zx_futex_wake failed: %d", status);
    return true;
}

bool PlatformFutex::Wait(uint32_t* value_ptr, int32_t current_value, uint64_t timeout_ns,
                         WaitResult* result_out)
{
    const zx_time_t deadline =
        (timeout_ns == UINT64_MAX) ? ZX_TIME_INFINITE : zx_deadline_after(timeout_ns);
    zx_status_t status =
        zx_futex_wait(reinterpret_cast<zx_futex_t*>(value_ptr), current_value, deadline);
    switch (status) {
        case ZX_OK:
            *result_out = AWOKE;
            break;
        case ZX_ERR_TIMED_OUT:
            *result_out = TIMED_OUT;
            break;
        case ZX_ERR_BAD_STATE:
            *result_out = RETRY;
            break;
        default:
            return DRETF(false, "zx_futex_wait returned: %d", status);
    }
    return true;
}

} // namespace magma
