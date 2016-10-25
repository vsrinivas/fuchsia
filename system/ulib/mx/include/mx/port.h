// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mx/handle.h>

namespace mx {

class port : public handle<port> {
public:
    port() = default;

    explicit port(handle<void>&& h) : handle(h.release()) {}

    port(port&& other) : handle(other.release()) {}

    port& operator=(port&& other) {
        reset(other.release());
        return *this;
    }

    static mx_status_t create(port* result, uint32_t options);

    mx_status_t queue(const void* packet, mx_size_t size) const {
        return mx_port_queue(get(), packet, size);
    }

    mx_status_t wait(void* packet, mx_size_t size) const {
        return mx_port_wait(get(), packet, size);
    }

    mx_status_t bind(uint64_t key, mx_handle_t source,
                     mx_signals_t signals) const {
        return mx_port_bind(get(), key, source, signals);
    }
};

} // namespace mx
