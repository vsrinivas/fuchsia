// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/channel.h>
#include <stdio.h>
#include <stdlib.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/types.h>

#include "garnet/lib/debugger_utils/util.h"

int main(int argc, char* argv[]) {
  printf("Test helper started.\n");

  zx::channel channel{zx_take_startup_handle(PA_HND(PA_USER0, 0))};
  // If no channel was passed we're running standalone.
  if (!channel.is_valid()) {
    fprintf(stderr, "Test helper: channel not received\n");
    return EXIT_FAILURE;
  }

  zx_handle_t thread = zx_thread_self();
  zx_status_t status = channel.write(0, nullptr, 0, &thread, 1);
  if (status != ZX_OK) {
    fprintf(stderr, "Test helper: channel write failed: %s\n",
            debugger_utils::ZxErrorString(status).c_str());
    return EXIT_FAILURE;
  }

  zx_signals_t pending;
  status = channel.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(),
                            &pending);
  if (status != ZX_OK) {
    fprintf(stderr, "Test helper: channel write failed: %s\n",
            debugger_utils::ZxErrorString(status).c_str());
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
