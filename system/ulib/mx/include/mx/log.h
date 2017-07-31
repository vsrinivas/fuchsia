// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mx/handle.h>
#include <mx/object.h>

namespace mx {

class log : public object<log> {
public:
    static constexpr mx_obj_type_t TYPE = MX_OBJ_TYPE_LOG;

    constexpr log() = default;

    explicit log(mx_handle_t value) : object(value) {}

    explicit log(handle&& h) : object(h.release()) {}

    log(log&& other) : object(other.release()) {}

    log& operator=(log&& other) {
        reset(other.release());
        return *this;
    }

    static mx_status_t create(log* result, uint32_t flags);

    mx_status_t write(uint32_t len, const void* buffer, uint32_t flags) const {
        return mx_log_write(get(), len, buffer, flags);
    }

    mx_status_t read(uint32_t len, void* buffer, uint32_t flags) const {
        return mx_log_read(get(), len, buffer, flags);
    }
};

using unowned_log = const unowned<log>;

} // namespace mx
