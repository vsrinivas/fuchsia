// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/log.h>

#include <zircon/syscalls.h>

namespace zx {

zx_status_t log::create(uint32_t flags, log* result) {
    zx_status_t status;
    zx_handle_t h;
    if ((status = zx_log_create(flags, &h)) < 0) {
        result->reset(ZX_HANDLE_INVALID);
        return status;
    } else {
        result->reset(h);
        return ZX_OK;
    }
}

} // namespace zx
