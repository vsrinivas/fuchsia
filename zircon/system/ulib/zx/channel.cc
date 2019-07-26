// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/channel.h>

#include <zircon/syscalls.h>

namespace zx {

zx_status_t channel::create(uint32_t flags, channel* endpoint0, channel* endpoint1) {
  // Ensure aliasing of both out parameters to the same container
  // has a well-defined result, and does not leak.
  channel h0;
  channel h1;
  zx_status_t status =
      zx_channel_create(flags, h0.reset_and_get_address(), h1.reset_and_get_address());
  endpoint0->reset(h0.release());
  endpoint1->reset(h1.release());
  return status;
}

}  // namespace zx
