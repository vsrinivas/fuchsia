// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/syscalls.h>

namespace zx {

namespace time {

inline zx_time_t get(uint32_t clock_id) {
    return zx_time_get(clock_id);
}

} // namespace time

inline zx_status_t nanosleep(zx_time_t deadline) {
    return zx_nanosleep(deadline);
}

inline zx_time_t deadline_after(zx_duration_t nanoseconds) {
    return zx_deadline_after(nanoseconds);
}

} // namespace zx
