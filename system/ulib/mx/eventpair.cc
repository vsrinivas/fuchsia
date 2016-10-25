// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mx/eventpair.h>

#include <magenta/syscalls.h>

namespace mx {

mx_status_t eventpair::create(eventpair* endpoint0, eventpair* endpoint1,
                              uint32_t flags) {
    mx_handle_t h[2];
    mx_status_t result = mx_eventpair_create(h, flags);
    endpoint0->reset(h[0]);
    endpoint1->reset(h[1]);
    return result;
}

} // namespace mx
