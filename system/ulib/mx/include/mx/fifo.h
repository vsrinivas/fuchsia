// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mx/handle.h>
#include <mx/object.h>

namespace mx {

class fifo : public object<fifo> {
public:
    static constexpr mx_obj_type_t TYPE = MX_OBJ_TYPE_FIFO;

    constexpr fifo() = default;

    explicit fifo(mx_handle_t value) : object(value) {}

    explicit fifo(handle&& h) : object(h.release()) {}

    fifo(fifo&& other) : object(other.release()) {}

    fifo& operator=(fifo&& other) {
        reset(other.release());
        return *this;
    }

    static mx_status_t create(uint32_t elem_count, uint32_t elem_size,
                              uint32_t options, fifo* out0, fifo* out1);

    mx_status_t write(const void* buffer, size_t len, uint32_t* actual_entries) const {
        return mx_fifo_write(get(), buffer, len, actual_entries);
    }

    mx_status_t read(void* buffer, size_t len, uint32_t* actual_entries) const {
        return mx_fifo_read(get(), buffer, len, actual_entries);
    }
};

using unowned_fifo = const unowned<fifo>;

} // namespace mx
