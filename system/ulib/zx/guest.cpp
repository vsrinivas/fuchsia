// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/guest.h>

#include <zircon/syscalls.h>

namespace zx {

zx_status_t guest::create(const resource& resource, uint32_t options,
                          const vmo& physmem, guest* result) {
    zx_handle_t h;
    zx_status_t status = zx_guest_create(resource.get(), options, physmem.get(), &h);
    if (status < 0) {
        result->reset(ZX_HANDLE_INVALID);
    } else {
        result->reset(h);
    }
    return status;
}

} // namespace zx
