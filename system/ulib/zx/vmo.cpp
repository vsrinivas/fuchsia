// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/vmo.h>

#include <zircon/syscalls.h>

namespace zx {

zx_status_t vmo::create(uint64_t size, uint32_t options, vmo* result) {
    zx_handle_t h = ZX_HANDLE_INVALID;
    zx_status_t status = zx_vmo_create(size, options, &h);
    result->reset(h);
    return status;
}

} // namespace zx
