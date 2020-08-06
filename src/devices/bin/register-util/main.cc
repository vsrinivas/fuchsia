// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <endian.h>
#include <fcntl.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <stdio.h>
#include <zircon/status.h>

#include "register-util.h"

int main(int argc, const char** argv) {
  if (argc < 4) {
    fprintf(stderr,
            "Usage: %s /path/to/device registeraddr registervalue\nregisteraddr and registervalue "
            "must both be formatted in hex.\n",
            argv[0]);
    return 0;
  }
  int fd = open(argv[1], O_RDWR);
  zx::channel channel;
  zx_status_t status = fdio_get_service_handle(fd, channel.reset_and_get_address());
  if (status != ZX_OK) {
    fprintf(stderr, "Unable to open register device due to error %s\n",
            zx_status_get_string(status));
    return -1;
  }
  return run(argc, argv, std::move(channel));
}
