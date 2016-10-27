// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mx/socket.h>

#include <magenta/syscalls.h>

namespace mx {

mx_status_t socket::create(uint32_t flags, socket* endpoint0, socket* endpoint1) {
    mx_handle_t h0 = MX_HANDLE_INVALID, h1 = MX_HANDLE_INVALID;
    mx_status_t result = mx_socket_create(flags, &h0, &h1);
    endpoint0->reset(h0);
    endpoint1->reset(h1);
    return result;
}

} // namespace mx
