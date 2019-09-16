// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fshost-fs-provider.h"

#include <lib/fdio/directory.h>

#include <stdio.h>
#include <string.h>


namespace devmgr {

zx::channel FshostFsProvider::CloneFs(const char* path) {
  if (strcmp(path, "svc") == 0) {
    path = "/svc";
  } else if (strcmp(path, "data") == 0) {
    path = "/fs/data";
  } else if (strcmp(path, "blob") == 0) {
    path = "/fs/blob";
  } else {
    printf("%s: Cannot clone: %s\n", __FUNCTION__, path);
    return zx::channel();
  }

  zx::channel client, server;
  zx_status_t status = zx::channel::create(0, &client, &server);
  if (status != ZX_OK) {
    return zx::channel();
  }
  status = fdio_service_connect(path, server.release());
  if (status != ZX_OK) {
    printf("%s: Failed to connect to %s: %d\n", __FUNCTION__, path, status);
    return zx::channel();
  }
  return client;
}

}  // namespace devmgr
