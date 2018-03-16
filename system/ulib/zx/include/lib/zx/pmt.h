// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/zx/handle.h>
#include <lib/zx/object.h>

namespace zx {

class pmt : public object<pmt> {
public:
    static constexpr zx_obj_type_t TYPE = ZX_OBJ_TYPE_PMT;

    constexpr pmt() = default;

    explicit pmt(zx_handle_t value) : object(value) {}

    explicit pmt(handle&& h) : object(h.release()) {}

    pmt(pmt&& other) : object(other.release()) {}

    pmt& operator=(pmt&& other) {
        reset(other.release());
        return *this;
    }

    zx_status_t unpin() {
        zx_status_t status = zx_pmt_unpin(get());
        if (status != ZX_OK) {
            return status;
        }
        zx_handle_t invld_handle __UNUSED = release();
        return ZX_OK;
    }
};

using unowned_pmt = const unowned<pmt>;

} // namespace zx
