// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <stdio.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

int main(int argc, char* argv[]) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <device path>\n", argv[0]);
    return -1;
  }

  const char* path = argv[1];

  zx_handle_t local, remote;
  zx_status_t status = zx_channel_create(0, &local, &remote);
  if (status != ZX_OK) {
    fprintf(stderr, "could not create channel\n");
    return -1;
  }
  status = fdio_service_connect(path, remote);
  if (status != ZX_OK) {
    fprintf(stderr, "could not open %s: %s\n", path, zx_status_get_string(status));
    return -1;
  }

  printf("suspending %s\n", path);
  zx_status_t call_status;
  status = fuchsia_device_ControllerDebugSuspend(local, &call_status);
  if (status == ZX_OK) {
    status = call_status;
  }
  if (status != ZX_OK) {
    fprintf(stderr, "suspend failed: %s\n", zx_status_get_string(status));
    return -1;
  }

  sleep(5);

  printf("resuming %s\n", path);
  status = fuchsia_device_ControllerDebugResume(local, &call_status);
  if (status == ZX_OK) {
    status = call_status;
  }
  if (status != ZX_OK) {
    fprintf(stderr, "resume failed: %s\n", zx_status_get_string(status));
    return -1;
  }
  return 0;
}
