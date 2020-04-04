// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fshost-fs-provider.h"

#include <lib/fdio/directory.h>
#include <stdio.h>
#include <string.h>

namespace devmgr {

zx::channel FshostFsProvider::CloneFs(const char* path) {
  int flags = FS_READ_WRITE_DIR_FLAGS;
  if (strcmp(path, "data") == 0) {
    path = "/fs/data";
  } else if (strcmp(path, "blobexec") == 0) {
    path = "/fs/blob";
    flags = FS_READ_WRITE_EXEC_DIR_FLAGS;
  } else {
    printf("%s: Cannot clone: %s\n", __FUNCTION__, path);
    return zx::channel();
  }

  zx::channel client, server;
  zx_status_t status = zx::channel::create(0, &client, &server);
  if (status != ZX_OK) {
    return zx::channel();
  }
  status = fdio_open(path, flags, server.release());
  if (status != ZX_OK) {
    printf("%s: Failed to connect to %s: %d\n", __FUNCTION__, path, status);
    return zx::channel();
  }
  return client;
}

}  // namespace devmgr
