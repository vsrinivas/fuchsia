// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/syscalls.h>

namespace mx {

namespace time {

inline mx_time_t get(uint32_t clock_id) {
    return mx_time_get(clock_id);
}

} // namespace time

inline mx_status_t nanosleep(mx_time_t deadline) {
    return mx_nanosleep(deadline);
}

inline mx_time_t deadline_after(mx_duration_t nanoseconds) {
    return mx_deadline_after(nanoseconds);
}

} // namespace mx
