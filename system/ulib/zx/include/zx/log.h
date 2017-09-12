// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zx/handle.h>
#include <zx/object.h>

namespace zx {

class log : public object<log> {
public:
    static constexpr zx_obj_type_t TYPE = ZX_OBJ_TYPE_LOG;

    constexpr log() = default;

    explicit log(zx_handle_t value) : object(value) {}

    explicit log(handle&& h) : object(h.release()) {}

    log(log&& other) : object(other.release()) {}

    log& operator=(log&& other) {
        reset(other.release());
        return *this;
    }

    static zx_status_t create(log* result, uint32_t flags);

    zx_status_t write(uint32_t len, const void* buffer, uint32_t flags) const {
        return zx_log_write(get(), len, buffer, flags);
    }

    zx_status_t read(uint32_t len, void* buffer, uint32_t flags) const {
        return zx_log_read(get(), len, buffer, flags);
    }
};

using unowned_log = const unowned<log>;

} // namespace zx
