// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/zx/handle.h>
#include <lib/zx/object.h>

namespace zx {

class vmo : public object<vmo> {
public:
    static constexpr zx_obj_type_t TYPE = ZX_OBJ_TYPE_VMO;

    constexpr vmo() = default;

    explicit vmo(zx_handle_t value) : object(value) {}

    explicit vmo(handle&& h) : object(h.release()) {}

    vmo(vmo&& other) : object(other.release()) {}

    vmo& operator=(vmo&& other) {
        reset(other.release());
        return *this;
    }

    static zx_status_t create(uint64_t size, uint32_t options, vmo* result);

    zx_status_t read(void* data, uint64_t offset, size_t len) const {
        return zx_vmo_read(get(), data, offset, len);
    }

    zx_status_t write(const void* data, uint64_t offset, size_t len) const {
        return zx_vmo_write(get(), data, offset, len);
    }

    zx_status_t get_size(uint64_t* size) const {
        return zx_vmo_get_size(get(), size);
    }

    zx_status_t set_size(uint64_t size) const {
        return zx_vmo_set_size(get(), size);
    }

    zx_status_t clone(uint32_t options, uint64_t offset, uint64_t size, vmo* result) const {
        zx_handle_t h = ZX_HANDLE_INVALID;
        zx_status_t status = zx_vmo_clone(get(), options, offset, size, &h);
        result->reset(h);
        return status;
    }

    zx_status_t op_range(uint32_t op, uint64_t offset, uint64_t size,
                         void* buffer, size_t buffer_size) const {
        return zx_vmo_op_range(get(), op, offset, size, buffer, buffer_size);
    }

    zx_status_t set_cache_policy(uint32_t cache_policy) {
        return zx_vmo_set_cache_policy(get(), cache_policy);
    }
};

using unowned_vmo = unowned<vmo>;

} // namespace zx
