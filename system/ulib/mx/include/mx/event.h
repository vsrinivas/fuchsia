// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mx/handle.h>
#include <mx/object.h>

namespace mx {

class event : public object<event> {
public:
    static constexpr mx_obj_type_t TYPE = MX_OBJ_TYPE_EVENT;

    constexpr event() = default;

    explicit event(mx_handle_t value) : object(value) {}

    explicit event(handle&& h) : object(h.release()) {}

    event(event&& other) : object(other.release()) {}

    event& operator=(event&& other) {
        reset(other.release());
        return *this;
    }

    static mx_status_t create(uint32_t options, event* result);
};

using unowned_event = const unowned<event>;

} // namespace mx
