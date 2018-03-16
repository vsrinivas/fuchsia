// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/zx/handle.h>
#include <lib/zx/object.h>
#include <lib/zx/resource.h>
#include <lib/zx/time.h>

namespace zx {

class interrupt : public object<interrupt> {
public:
    static constexpr zx_obj_type_t TYPE = ZX_OBJ_TYPE_INTERRUPT;

    constexpr interrupt() = default;

    explicit interrupt(zx_handle_t value) : object(value) {}

    explicit interrupt(handle&& h) : object(h.release()) {}

    interrupt(interrupt&& other) : object(other.release()) {}

    interrupt& operator=(interrupt&& other) {
        reset(other.release());
        return *this;
    }

    static zx_status_t create(const resource& resource, uint32_t options, interrupt* result);

    zx_status_t bind(uint32_t slot, const resource& resource, uint32_t vector, uint32_t options) {
        return zx_interrupt_bind(get(), slot, resource.get(), vector, options);
    }

    zx_status_t wait(uint64_t* slots) {
        return zx_interrupt_wait(get(), slots);
    }

    zx_status_t get_timestamp(uint32_t slot, zx::time* timestamp) {
        return zx_interrupt_get_timestamp(get(), slot, timestamp->get_address());
    }

    zx_status_t signal(uint32_t slot, zx::time timestamp) {
        return zx_interrupt_signal(get(), slot, timestamp.get());
    }
};

using unowned_interrupt = const unowned<interrupt>;

} // namespace zx
