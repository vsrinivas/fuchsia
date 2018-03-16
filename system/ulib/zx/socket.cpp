// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/socket.h>

#include <zircon/syscalls.h>

namespace zx {

zx_status_t socket::create(uint32_t flags, socket* endpoint0,
                           socket* endpoint1) {
    zx_handle_t h0 = ZX_HANDLE_INVALID, h1 = ZX_HANDLE_INVALID;
    zx_status_t result = zx_socket_create(flags, &h0, &h1);
    endpoint0->reset(h0);
    endpoint1->reset(h1);
    return result;
}

} // namespace zx
