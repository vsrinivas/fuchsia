// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/eventpair.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

int main() {
  // PA_USER0 is an event pair passed to us by the test.
  zx::eventpair event{zx_take_startup_handle(PA_HND(PA_USER0, 0))};

  if (event.signal_peer(0u, ZX_EVENTPAIR_SIGNALED) != ZX_OK) {
    return 1;
  }

  return 4321;
}
