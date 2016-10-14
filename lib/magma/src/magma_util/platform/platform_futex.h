// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PLATFORM_FUTEX_H
#define PLATFORM_FUTEX_H

#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include <stdint.h>

namespace magma {

class PlatformFutex {
public:
    static bool Wake(uint32_t* value_ptr, int32_t wake_count);

    enum WaitResult { AWOKE, TIMED_OUT, RETRY };

    static bool Wait(uint32_t* value_ptr, int32_t current_value, uint64_t timeout_ns,
                     WaitResult* result_out);

    static bool WaitForever(uint32_t* value_ptr, int32_t current_value, WaitResult* result_out)
    {
        return Wait(value_ptr, current_value, UINT64_MAX, result_out);
    }
};

} // namespace magma

#endif // PLATFORM_FUTEX_H
