// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/eventpair.h>

#include <zircon/syscalls.h>

namespace zx {

zx_status_t eventpair::create(uint32_t flags, eventpair* endpoint0, eventpair* endpoint1) {
  // Ensure aliasing of both out parameters to the same container
  // has a well-defined result, and does not leak.
  eventpair h0;
  eventpair h1;
  zx_status_t status =
      zx_eventpair_create(flags, h0.reset_and_get_address(), h1.reset_and_get_address());
  endpoint0->reset(h0.release());
  endpoint1->reset(h1.release());
  return status;
}

}  // namespace zx
