// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zx/handle.h>
#include <zx/object.h>
#include <zx/vmo.h>

namespace zx {

class bti : public object<bti> {
public:
    static constexpr zx_obj_type_t TYPE = ZX_OBJ_TYPE_BTI;

    constexpr bti() = default;

    explicit bti(zx_handle_t value) : object(value) {}

    explicit bti(handle&& h) : object(h.release()) {}

    bti(bti&& other) : object(other.release()) {}

    bti& operator=(bti&& other) {
        reset(other.release());
        return *this;
    }

    zx_status_t pin(uint32_t options, const vmo& vmo, uint64_t offset, uint64_t size,
                    zx_paddr_t* addrs, size_t addrs_count) const {
        return zx_bti_pin(get(), options, vmo.get(), offset, size, addrs, addrs_count);
    }

    zx_status_t unpin(zx_paddr_t base) const {
        return zx_bti_unpin(get(), base);
    }
};

using unowned_bti = const unowned<bti>;

} // namespace zx
