// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mx/handle.h>

namespace mx {

class vmo : public handle<vmo> {
public:
    vmo() = default;

    explicit vmo(handle<void>&& h) : handle(h.release()) {}

    vmo(vmo&& other) : handle(other.release()) {}

    vmo& operator=(vmo&& other) {
        reset(other.release());
        return *this;
    }

    static mx_status_t create(vmo* result, uint64_t size);

    mx_ssize_t read(void* data, uint64_t offset, mx_size_t len) const {
        return mx_vmo_read(get(), data, offset, len);
    }

    mx_ssize_t write(const void* data, uint64_t offset, mx_size_t len) const {
        return mx_vmo_write(get(), data, offset, len);
    }

    mx_status_t get_size(uint64_t* size) const {
        return mx_vmo_get_size(get(), size);
    }

    mx_status_t set_size(uint64_t size) const {
        return mx_vmo_set_size(get(), size);
    }

    mx_status_t op_range(uint32_t op, uint64_t offset, uint64_t size,
                         void* buffer, mx_size_t buffer_size) const {
        return mx_vmo_op_range(get(), op, offset, size, buffer, buffer_size);
    }
};

} // namespace mx
