// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/usb/debug/c/fidl.h>
#include <lib/fdio/cpp/caller.h>

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>

#include <utility>

#include "xdc-init.h"

static const char* const DEV_XDC_DIR = "/dev/class/usb-dbc";

zx_status_t configure_xdc(uint32_t stream_id, fbl::unique_fd* out_fd) {
  DIR* d = opendir(DEV_XDC_DIR);
  if (d == nullptr) {
    fprintf(stderr, "Could not open dir: \"%s\"\n", DEV_XDC_DIR);
    return ZX_ERR_BAD_STATE;
  }

  struct dirent* de;
  while ((de = readdir(d)) != nullptr) {
    int fd = openat(dirfd(d), de->d_name, O_RDWR);
    if (fd < 0) {
      continue;
    }
    fdio_cpp::FdioCaller caller{fbl::unique_fd(fd)};
    zx_status_t status;
    zx_status_t res =
        fuchsia_usb_debug_DeviceSetStream(caller.borrow_channel(), stream_id, &status);
    if (res == ZX_OK) {
      res = status;
    }
    if (res != ZX_OK) {
      fprintf(stderr, "Failed to set stream id %u for device \"%s/%s\", err: %d\n", stream_id,
              DEV_XDC_DIR, de->d_name, res);
      continue;
    }
    printf("Configured debug device \"%s/%s\", stream id %u\n", DEV_XDC_DIR, de->d_name, stream_id);
    *out_fd = caller.release();
    closedir(d);
    return ZX_OK;
  }
  closedir(d);

  fprintf(stderr, "No debug device found\n");
  return ZX_ERR_NOT_FOUND;
}
