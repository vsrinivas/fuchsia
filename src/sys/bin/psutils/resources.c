// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "resources.h"

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/boot/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

zx_status_t get_root_resource(zx_handle_t* root_resource) {
  zx_handle_t local, remote;
  zx_status_t status = zx_channel_create(0, &local, &remote);
  if (status != ZX_OK) {
    return status;
  }
  status = fdio_service_connect("/svc/fuchsia.boot.RootResource", remote);
  if (status != ZX_OK) {
    fprintf(stderr, "ERROR: Cannot open fuchsia.boot.RootResource: %s (%d)\n",
            zx_status_get_string(status), status);
    zx_handle_close(local);
    return ZX_ERR_NOT_FOUND;
  }

  zx_status_t fidl_status = fuchsia_boot_RootResourceGet(local, root_resource);
  zx_handle_close(local);

  if (fidl_status != ZX_OK) {
    fprintf(stderr, "ERROR: Cannot obtain root resource: %s (%d)\n",
            zx_status_get_string(fidl_status), fidl_status);
    return fidl_status;
  }

  return ZX_OK;
}
