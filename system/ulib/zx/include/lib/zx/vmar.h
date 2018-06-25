// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/zx/object.h>
#include <lib/zx/vmo.h>
#include <zircon/process.h>

namespace zx {

// A wrapper for handles to VMARs.  Note that vmar::~vmar() does not execute
// vmar::destroy(), it just closes the handle.
class vmar : public object<vmar> {
public:
    static constexpr zx_obj_type_t TYPE = ZX_OBJ_TYPE_VMAR;

    constexpr vmar() = default;

    explicit vmar(zx_handle_t value) : object(value) {}

    explicit vmar(handle&& h) : object(h.release()) {}

    vmar(vmar&& other) : vmar(other.release()) {}

    vmar& operator=(vmar&& other) {
        reset(other.release());
        return *this;
    }

    zx_status_t map(size_t vmar_offset, const vmo& vmo_handle, uint64_t vmo_offset,
                    size_t len, uint32_t flags, uintptr_t* ptr) const {
        return zx_vmar_map(get(), vmar_offset, vmo_handle.get(), vmo_offset, len, flags, ptr);
    }

    zx_status_t unmap(uintptr_t address, size_t len) const {
        return zx_vmar_unmap(get(), address, len);
    }

    zx_status_t protect(uintptr_t address, size_t len, uint32_t prot) const {
        return zx_vmar_protect(get(), address, len, prot);
    }

    zx_status_t destroy() const {
        return zx_vmar_destroy(get());
    }

    zx_status_t allocate(size_t offset, size_t size, uint32_t flags,
                         vmar* child, uintptr_t* child_addr) const;

    static inline const legacy_unowned<vmar> root_self() {
        return legacy_unowned<vmar>(zx_vmar_root_self());
    }
};

using unowned_vmar = unowned<vmar>;

} // namespace zx
