// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/port.h>

#include <zircon/syscalls.h>

namespace zx {

zx_status_t port::create(uint32_t options, port* result) {
    return zx_port_create(options, result->reset_and_get_address());
}

} // namespace zx
