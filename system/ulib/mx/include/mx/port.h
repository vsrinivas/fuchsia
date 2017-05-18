// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mx/handle.h>
#include <mx/object.h>

namespace mx {

class port : public object<port> {
public:
    static constexpr mx_obj_type_t TYPE = MX_OBJ_TYPE_IOPORT;

    port() = default;

    explicit port(mx_handle_t value) : object(value) {}

    explicit port(handle&& h) : object(h.release()) {}

    port(port&& other) : object(other.release()) {}

    port& operator=(port&& other) {
        reset(other.release());
        return *this;
    }

    static mx_status_t create(uint32_t options, port* result);

    mx_status_t queue(const void* packet, size_t size) const {
        return mx_port_queue(get(), packet, size);
    }

    mx_status_t wait(mx_time_t deadline, void* packet, size_t size) const {
        return mx_port_wait(get(), deadline, packet, size);
    }

    mx_status_t bind(uint64_t key, mx_handle_t source,
                     mx_signals_t signals) const {
        return mx_port_bind(get(), key, source, signals);
    }

    mx_status_t cancel(mx_handle_t source, uint64_t key) const {
        return mx_port_cancel(get(), source, key);
    }
};

} // namespace mx
