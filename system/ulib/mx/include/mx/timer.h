// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mx/handle.h>
#include <mx/object.h>

#include <magenta/types.h>

namespace mx {

class timer : public object<timer> {
public:
    static constexpr mx_obj_type_t TYPE = MX_OBJ_TYPE_TIMER;

    constexpr timer() = default;

    explicit timer(mx_handle_t value) : object(value) {}

    explicit timer(handle&& h) : object(h.release()) {}

    timer(timer&& other) : object(other.release()) {}

    timer& operator=(timer&& other) {
        reset(other.release());
        return *this;
    }

    static mx_status_t create(uint32_t options, uint32_t clock_id, timer* result);

    mx_status_t set(mx_time_t deadline, mx_duration_t slack) const {
        return mx_timer_set(get(), deadline, slack);
    }

    mx_status_t cancel() const {
        return mx_timer_cancel(get());
    }
};

using unowned_timer = const unowned<timer>;

} // namespace mx
