// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdio>
#include <cstring>

#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

static int test_attach() {
  auto channel = zx_take_startup_handle(PA_HND(PA_USER0, 0));
  auto status = zx_object_wait_one(channel, ZX_CHANNEL_PEER_CLOSED,
                                   ZX_TIME_INFINITE, nullptr);
  if (status != ZX_OK) {
    fprintf(stderr, "zx_object_wait_one failed: %d/%s\n", status,
            zx_status_get_string(status));
    return 1;
  }
  printf("test-attach complete, peer channel closed\n");
  return 0;
}

int main(int argc, char* argv[]) {
  if (argc > 2) {
    fprintf(stderr, "Usage: %s [command]\n", argv[0]);
    return 1;
  }

  if (argc == 2) {
    const char* cmd = argv[1];
    if (strcmp(cmd, "test-attach") == 0) {
      return test_attach();
    }
    fprintf(stderr, "Unrecognized command: %s\n", cmd);
    return 1;
  }

  printf("Hello.\n");
  return 0;
}
