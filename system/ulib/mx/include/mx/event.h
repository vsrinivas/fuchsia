// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mx/handle.h>

namespace mx {

class event : public handle<event> {
public:
    event() = default;

    explicit event(handle<void>&& h) : handle(h.release()) {}

    event(event&& other) : handle(other.release()) {}

    event& operator=(event&& other) {
        reset(other.release());
        return *this;
    }

    static mx_status_t create(uint32_t options, event* result);
};

} // namespace mx
