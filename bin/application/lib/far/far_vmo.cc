// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/lib/far/far.h"

#include <magenta/syscalls.h>
#include <mxio/io.h>

bool far_reader_read_vmo(far_reader_t reader, mx_handle_t vmo) {
  uint64_t num_bytes = 0;
  mx_status_t status = mx_vmo_get_size(vmo, &num_bytes);
  if (status != MX_OK)
    return false;
  int fd = mxio_vmo_fd(vmo, 0, num_bytes);
  if (fd < 0)
    return false;
  return far_reader_read_fd(reader, fd);
}
