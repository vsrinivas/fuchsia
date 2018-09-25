// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/debuglog.h>

#include <zircon/syscalls.h>

namespace zx {

zx_status_t debuglog::create(const resource& resource, uint32_t options, debuglog* result) {
    return zx_debuglog_create(resource.get(), options, result->reset_and_get_address());
}

} // namespace zx
