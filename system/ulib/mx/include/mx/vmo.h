// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mx/handle.h>
#include <mx/object.h>

namespace mx {

class vmo : public object<vmo> {
public:
    static constexpr mx_obj_type_t TYPE = MX_OBJ_TYPE_VMEM;

    vmo() = default;

    explicit vmo(mx_handle_t value) : object(value) {}

    explicit vmo(handle&& h) : object(h.release()) {}

    vmo(vmo&& other) : object(other.release()) {}

    vmo& operator=(vmo&& other) {
        reset(other.release());
        return *this;
    }

    static mx_status_t create(uint64_t size, uint32_t options, vmo* result);

    mx_status_t read(void* data, uint64_t offset, size_t len,
                     size_t* actual) const {
        return mx_vmo_read(get(), data, offset, len, actual);
    }

    mx_status_t write(const void* data, uint64_t offset, size_t len,
                      size_t* actual) const {
        return mx_vmo_write(get(), data, offset, len, actual);
    }

    mx_status_t get_size(uint64_t* size) const {
        return mx_vmo_get_size(get(), size);
    }

    mx_status_t set_size(uint64_t size) const {
        return mx_vmo_set_size(get(), size);
    }

    mx_status_t clone(uint32_t options, uint64_t offset, uint64_t size, vmo* result) {
        mx_handle_t h = MX_HANDLE_INVALID;
        mx_status_t status = mx_vmo_clone(get(), options, offset, size, &h);
        result->reset(h);
        return status;
    }

    mx_status_t op_range(uint32_t op, uint64_t offset, uint64_t size,
                         void* buffer, size_t buffer_size) const {
        return mx_vmo_op_range(get(), op, offset, size, buffer, buffer_size);
    }
};

} // namespace mx
