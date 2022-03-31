// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fshost-fs-provider.h"

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/syslog/cpp/macros.h>
#include <stdio.h>
#include <string.h>

namespace fshost {

zx::channel FshostFsProvider::CloneFs(const char* path) {
  fuchsia_io::wire::OpenFlags flags =
      fuchsia_io::wire::OpenFlags::kRightReadable | fuchsia_io::wire::OpenFlags::kRightWritable |
      fuchsia_io::wire::OpenFlags::kDirectory | fuchsia_io::wire::OpenFlags::kNoRemote;
  if (strcmp(path, "data") == 0) {
    path = "/fs/data";
  } else if (strcmp(path, "blobexec") == 0) {
    path = "/blob";
    flags |= fuchsia_io::wire::OpenFlags::kRightExecutable;
  } else {
    FX_LOGS(ERROR) << "" << __FUNCTION__ << ": Cannot clone: " << path;
    return zx::channel();
  }

  zx::channel client, server;
  zx_status_t status = zx::channel::create(0, &client, &server);
  if (status != ZX_OK) {
    return zx::channel();
  }
  status = fdio_open(path, static_cast<uint32_t>(flags), server.release());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "" << __FUNCTION__ << ": Failed to connect to " << path << ": " << status;
    return zx::channel();
  }
  return client;
}

}  // namespace fshost
