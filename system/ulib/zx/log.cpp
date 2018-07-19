// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/log.h>

#include <zircon/syscalls.h>

namespace zx {

zx_status_t log::create(uint32_t flags, log* result) {
    return zx_debuglog_create(ZX_HANDLE_INVALID, flags, result->reset_and_get_address());
}

} // namespace zx
