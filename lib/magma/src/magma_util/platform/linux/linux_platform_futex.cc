// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform_futex.h"

namespace magma {

bool PlatformFutex::Wake(uint32_t* value_ptr, int32_t wake_count)
{
    // TODO(MA-520): implement virtio-magma
    return true;
}

bool PlatformFutex::Wait(uint32_t* value_ptr, int32_t current_value, uint64_t timeout_ns,
                         WaitResult* result_out)
{
    // TODO(MA-520): implement virtio-magma
    return true;
}

} // namespace magma
