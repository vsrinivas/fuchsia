// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/pty/llcpp/fidl.h>

#include "pty_fuchsia.h"

namespace fpty = ::llcpp::fuchsia::hardware::pty;

zx_status_t pty_read_events(zx_handle_t handle, uint32_t* out_events) {
  auto result = fpty::Device::Call::ReadEvents(zx::unowned_channel(handle));
  if (result.status() != ZX_OK) {
    return result.status();
  }
  if (result->status != ZX_OK) {
    return result->status;
  }
  *out_events = result->events;
  return ZX_OK;
}
