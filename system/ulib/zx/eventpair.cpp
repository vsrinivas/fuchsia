// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/eventpair.h>

#include <zircon/syscalls.h>

namespace zx {

zx_status_t eventpair::create(uint32_t flags, eventpair* endpoint0,
                              eventpair* endpoint1) {
    zx_handle_t h0 = ZX_HANDLE_INVALID;
    zx_handle_t h1 = ZX_HANDLE_INVALID;
    zx_status_t result = zx_eventpair_create(flags, &h0, &h1);
    endpoint0->reset(h0);
    endpoint1->reset(h1);
    return result;
}

} // namespace zx
