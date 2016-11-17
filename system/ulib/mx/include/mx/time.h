// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/syscalls.h>

namespace mx {

namespace time {

mx_time_t get(uint32_t clock_id) {
    return mx_time_get(clock_id);
}

} // namespace time

mx_status_t nanosleep(uint64_t nanoseconds) {
    return mx_nanosleep(nanoseconds);
}

} // namespace mx
