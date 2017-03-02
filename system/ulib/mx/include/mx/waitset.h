// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mx/handle.h>
#include <mx/object.h>

namespace mx {

class waitset : public object<waitset> {
public:
    static constexpr mx_obj_type_t TYPE = MX_OBJ_TYPE_WAIT_SET;

    waitset() = default;

    explicit waitset(mx_handle_t value) : object(value) {}

    explicit waitset(handle&& h) : object(h.release()) {}

    waitset(waitset&& other) : object(other.release()) {}

    waitset& operator=(waitset&& other) {
        reset(other.release());
        return *this;
    }

    static mx_status_t create(uint32_t options, waitset* result);

    mx_status_t add(uint64_t cookie, mx_handle_t handle,
                    mx_signals_t signals) const {
        return mx_waitset_add(get(), cookie, handle, signals);
    }

    mx_status_t remove(uint64_t cookie) const {
        return mx_waitset_remove(get(), cookie);
    }

    mx_status_t wait(mx_time_t timeout, mx_waitset_result_t* results,
                     uint32_t* num_results) const {
        return mx_waitset_wait(get(), timeout, results, num_results);
    }
};

} // namespace mx
