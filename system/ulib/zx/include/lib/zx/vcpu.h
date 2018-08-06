// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/zx/guest.h>
#include <lib/zx/handle.h>
#include <lib/zx/object.h>
#include <zircon/syscalls/port.h>

namespace zx {

class vcpu : public object<vcpu> {
public:
    static constexpr zx_obj_type_t TYPE = ZX_OBJ_TYPE_VCPU;

    constexpr vcpu() = default;

    explicit vcpu(zx_handle_t value) : object(value) {}

    explicit vcpu(handle&& h) : object(h.release()) {}

    vcpu(vcpu&& other) : object(other.release()) {}

    vcpu& operator=(vcpu&& other) {
        reset(other.release());
        return *this;
    }

    static zx_status_t create(const guest& guest, uint32_t options,
                              zx_gpaddr_t entry, vcpu* result);

    zx_status_t resume(zx_port_packet_t* packet) {
        return zx_vcpu_resume(get(), packet);
    }

    zx_status_t interrupt(uint32_t interrupt) {
        return zx_vcpu_interrupt(get(), interrupt);
    }

    zx_status_t read_state(uint32_t kind, void* buf, size_t len) const {
        return zx_vcpu_read_state(get(), kind, buf, len);
    }

    zx_status_t write_state(uint32_t kind, const void* buf, size_t len) {
        return zx_vcpu_write_state(get(), kind, buf, len);
    }
};

using unowned_vcpu = unowned<vcpu>;

} // namespace zx
