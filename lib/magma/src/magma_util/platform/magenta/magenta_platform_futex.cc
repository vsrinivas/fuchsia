// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magenta/syscalls.h"
#include "platform_futex.h"

namespace magma {

static_assert(sizeof(mx_futex_t) == sizeof(uint32_t), "futex type incompatible size");

bool PlatformFutex::Wake(uint32_t* value_ptr, int32_t wake_count)
{
    mx_status_t status;
    if ((status = mx_futex_wake(reinterpret_cast<mx_futex_t*>(value_ptr), wake_count)) != NO_ERROR)
        return DRETF(false, "mx_futex_wake failed: %d", status);
    return true;
}

bool PlatformFutex::Wait(uint32_t* value_ptr, int32_t current_value, uint64_t timeout_ns,
                         WaitResult* result_out)
{
    mx_status_t status =
        mx_futex_wait(reinterpret_cast<mx_futex_t*>(value_ptr), current_value, timeout_ns);
    switch (status) {
    case NO_ERROR:
        *result_out = AWOKE;
        break;
    case ERR_TIMED_OUT:
        *result_out = TIMED_OUT;
        break;
    case ERR_BAD_STATE:
        *result_out = RETRY;
        break;
    default:
        return DRETF(false, "mx_futex_wait returned: %d", status);
    }
    return true;
}

} // namespace