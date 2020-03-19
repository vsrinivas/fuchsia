// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/llcpp/fidl.h>
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
    fprintf(stderr, "could not open %s: %s\n", argv[1], zx_status_get_string(status));
    return -1;
  }

  char path[1025];
  size_t actual_len;

  auto resp =
      ::llcpp::fuchsia::device::Controller::Call::GetTopologicalPath(zx::unowned_channel(local));
  status = resp.status();

  if (status == ZX_OK) {
    if (resp->result.is_err()) {
      status = resp->result.err();
    } else {
      actual_len = resp->result.response().path.size();
      auto& r = resp->result.response();
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
