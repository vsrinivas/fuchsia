// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test_support.h"

#include <fcntl.h>
#include <limits.h>

#include <fbl/unique_fd.h>
#include <fuchsia/device/c/fidl.h>
#include <lib/fzl/fdio.h>
#include <zircon/status.h>

std::string GetTopologicalPath(const std::string& path) {
  fbl::unique_fd fd(open(path.c_str(), O_RDWR));
  if (!fd) {
    printf("Could not open block device\n");
    return std::string();
  }
  fzl::FdioCaller caller(std::move(fd));
  return GetTopologicalPath(caller.borrow_channel());
}

std::string GetTopologicalPath(zx_handle_t channel) {
  zx_status_t status;
  size_t path_len;
  char disk_path[PATH_MAX];
  zx_status_t io_status = fuchsia_device_ControllerGetTopologicalPath(
      channel, &status, disk_path, sizeof(disk_path) - 1, &path_len);
  if (io_status != ZX_OK) {
    status = io_status;
  }
  if (status != ZX_OK || path_len > sizeof(disk_path) - 1) {
    printf("Could not acquire topological path of block device: %s\n",
           zx_status_get_string(status));
    return std::string();
  }

  disk_path[path_len] = 0;
  return std::string(disk_path);
}
