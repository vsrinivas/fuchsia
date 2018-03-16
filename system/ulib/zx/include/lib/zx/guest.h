// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/zx/handle.h>
#include <lib/zx/object.h>
#include <lib/zx/port.h>
#include <lib/zx/resource.h>
#include <lib/zx/vmo.h>

namespace zx {

class guest : public object<guest> {
public:
    static constexpr zx_obj_type_t TYPE = ZX_OBJ_TYPE_GUEST;

    constexpr guest() = default;

    explicit guest(zx_handle_t value) : object(value) {}

    explicit guest(handle&& h) : object(h.release()) {}

    guest(guest&& other) : object(other.release()) {}

    guest& operator=(guest&& other) {
        reset(other.release());
        return *this;
    }

    static zx_status_t create(const resource& resource, uint32_t options,
                              const vmo& physmem, guest* result);

    zx_status_t set_trap(uint32_t kind, zx_vaddr_t addr, size_t len,
                         const port& port, uint64_t key) {
        return zx_guest_set_trap(get(), kind, addr, len, port.get(), key);
    }
};

using unowned_guest = const unowned<guest>;

} // namespace zx
