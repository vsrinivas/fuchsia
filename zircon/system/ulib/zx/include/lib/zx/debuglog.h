// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZX_DEBUGLOG_H_
#define LIB_ZX_DEBUGLOG_H_

#include <lib/zx/handle.h>
#include <lib/zx/object.h>
#include <lib/zx/resource.h>

namespace zx {

class debuglog : public object<debuglog> {
public:
    static constexpr zx_obj_type_t TYPE = ZX_OBJ_TYPE_LOG;

    constexpr debuglog() = default;

    explicit debuglog(zx_handle_t value) : object(value) {}

    explicit debuglog(handle&& h) : object(h.release()) {}

    debuglog(debuglog&& other) : object(other.release()) {}

    debuglog& operator=(debuglog&& other) {
        reset(other.release());
        return *this;
    }

    static zx_status_t create(const resource& resource, uint32_t options, debuglog* result);

    zx_status_t write(uint32_t options, const void* buffer, size_t buffer_size) const {
        return zx_debuglog_write(get(), options, buffer, buffer_size);
    }

    zx_status_t read(uint32_t options, void* buffer, size_t buffer_size) const {
        return zx_debuglog_read(get(), options, buffer, buffer_size);
    }
};

using unowned_debuglog = unowned<debuglog>;

} // namespace zx

#endif  // LIB_ZX_DEBUGLOG_H_
