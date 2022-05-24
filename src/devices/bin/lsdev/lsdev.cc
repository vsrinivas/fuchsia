// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.device/cpp/wire.h>
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

  zx_handle_t local, remote;
  zx_status_t status = zx_channel_create(0, &local, &remote);
  if (status != ZX_OK) {
    fprintf(stderr, "could not create channel\n");
    return -1;
  }
  status = fdio_service_connect(argv[1], remote);
  if (status != ZX_OK) {
    zx_handle_close(local);
    fprintf(stderr, "could not open %s: %s\n", argv[1], zx_status_get_string(status));
    return -1;
  }

  char path[1025];
  size_t actual_len;

  auto resp =
      fidl::WireCall<fuchsia_device::Controller>(zx::unowned_channel(local))->GetTopologicalPath();
  status = resp.status();

  if (status == ZX_OK) {
    if (resp.Unwrap_NEW()->is_error()) {
      status = resp.Unwrap_NEW()->error_value();
    } else {
      actual_len = resp.Unwrap_NEW()->value()->path.size();
      auto& r = *resp.Unwrap_NEW()->value();
      memcpy(path, r.path.data(), r.path.size());
    }
  }

  if (status != ZX_OK) {
    fprintf(stderr, "could not get topological path for %s: %s\n", argv[1],
            zx_status_get_string(status));
    return -1;
  }
  path[actual_len] = 0;

  printf("topological path for %s: %s\n", argv[1], path);
  return 0;
}
