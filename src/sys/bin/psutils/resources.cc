// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "resources.h"

#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.boot/cpp/fidl.h>
#include <lib/component/incoming/cpp/service_client.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

zx_status_t get_root_resource(zx_handle_t* root_resource) {
  auto client_end = component::Connect<fuchsia_boot::RootResource>();
  if (client_end.is_error()) {
    fprintf(stderr, "ERROR: Cannot open fuchsia.boot.RootResource: %s (%d)\n",
            client_end.status_string(), client_end.status_value());
    return ZX_ERR_NOT_FOUND;
  }
  auto result = fidl::WireSyncClient(std::move(*client_end))->Get();
  if (result.status() != ZX_OK) {
    fprintf(stderr, "ERROR: Cannot obtain root resource: %s (%d)\n",
            zx_status_get_string(result.status()), result.status());
    return ZX_ERR_NOT_FOUND;
  }

  *root_resource = result->resource.release();

  return ZX_OK;
}
