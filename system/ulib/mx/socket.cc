// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mx/socket.h>

#include <magenta/syscalls.h>

namespace mx {

mx_status_t socket::create(socket* endpoint0, socket* endpoint1,
                           uint32_t flags) {
    mx_handle_t h[2];
    mx_status_t result = mx_socket_create(h, flags);
    endpoint0->reset(h[0]);
    endpoint1->reset(h[1]);
    return result;
}

} // namespace mx
