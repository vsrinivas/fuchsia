// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mx/handle.h>

namespace mx {

class log : public handle<log> {
public:
    log() = default;

    explicit log(mx_handle_t value) : handle(value) {}

    explicit log(handle<void>&& h) : handle(h.release()) {}

    log(log&& other) : handle(other.release()) {}

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

} // namespace mx
