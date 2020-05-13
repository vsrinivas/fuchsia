// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/lib/grpc/fdio_util.h"

#include <fcntl.h>
#include <lib/fdio/fd.h>
#include <lib/syslog/cpp/macros.h>

int ConvertSocketToNonBlockingFd(zx::socket socket) {
  int fd = -1;
  zx_status_t status = fdio_fd_create(socket.release(), &fd);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Could not get client fdio endpoint";
    return -1;
  }

  auto flags = fcntl(fd, F_GETFL);
  if (flags == -1) {
    FX_LOGS(ERROR) << "fcntl(F_GETFL) failed: " << strerror(errno);
    return -1;
  }
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    FX_LOGS(ERROR) << "fcntl(F_SETFL) failed: " << strerror(errno);
    return -1;
  }
  return fd;
}
