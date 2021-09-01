// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pty_fuchsia.h"

#include <fidl/fuchsia.hardware.pty/cpp/wire.h>

namespace fpty = fuchsia_hardware_pty;

zx_status_t pty_read_events(zx_handle_t handle, uint32_t* out_events) {
  auto result = fidl::WireCall<fpty::Device>(zx::unowned_channel(handle)).ReadEvents();
  if (result.status() != ZX_OK) {
    return result.status();
  }
  if (result->status != ZX_OK) {
    return result->status;
  }
  *out_events = result->events;
  return ZX_OK;
}
