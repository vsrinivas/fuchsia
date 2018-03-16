// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/channel.h>

#include <zircon/syscalls.h>

namespace zx {

zx_status_t channel::create(uint32_t flags, channel* endpoint0,
                            channel* endpoint1) {
    zx_handle_t h0, h1;
    zx_status_t result = zx_channel_create(flags, &h0, &h1);
    endpoint0->reset(h0);
    endpoint1->reset(h1);
    return result;
}

} // namespace zx
