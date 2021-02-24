// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/lib/grpc/fdio_util.h"

#include <errno.h>
#include <fcntl.h>

int SetNonBlocking(fbl::unique_fd& fd) {
  int flags = fcntl(fd.get(), F_GETFL);
  if (flags < 0) {
    return errno;
  }
  if (fcntl(fd.get(), F_SETFL, flags | O_NONBLOCK) < 0) {
    return errno;
  }
  return 0;
}
