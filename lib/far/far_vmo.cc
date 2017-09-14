// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/far/far.h"

#include <zircon/syscalls.h>
#include <fdio/io.h>

bool far_reader_read_vmo(far_reader_t reader, zx_handle_t vmo) {
  uint64_t num_bytes = 0;
  zx_status_t status = zx_vmo_get_size(vmo, &num_bytes);
  if (status != ZX_OK)
    return false;
  int fd = fdio_vmo_fd(vmo, 0, num_bytes);
  if (fd < 0)
    return false;
  return far_reader_read_fd(reader, fd);
}
