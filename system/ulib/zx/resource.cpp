// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/resource.h>

#include <zircon/syscalls.h>

namespace zx {

zx_status_t resource::create(const resource& parent, uint32_t kind, uint64_t low,
                             uint64_t high, resource* result) {
    zx_handle_t h;
    zx_status_t status = zx_resource_create(parent.get(), kind, low, high, &h);
    if (status < 0) {
        result->reset(ZX_HANDLE_INVALID);
    } else {
        result->reset(h);
    }
    return status;
}

} // namespace zx
