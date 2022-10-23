// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/result.h>
#include <lib/zx/stream.h>
#include <lib/zx/vmo.h>

// This header contains the public definition of memfd_create and is necessary to ensure the
// function gets the correct linkage.
#include <sys/mman.h>

#include <algorithm>

#include "sdk/lib/fdio/fdio_unistd.h"
#include "sdk/lib/fdio/zxio.h"

__EXPORT
int memfd_create(const char* name, unsigned int flags) {
  if (flags) {
    return ERRNO(EINVAL);
  }
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(0u, ZX_VMO_RESIZABLE, &vmo);
  if (status != ZX_OK) {
    return ERROR(status);
  }

  if (name) {
    status = vmo.set_property(ZX_PROP_NAME, name, std::min(ZX_MAX_NAME_LEN, strlen(name)));
    if (status != ZX_OK) {
      return ERROR(status);
    }
  }

  zx::stream stream;
  status = zx::stream::create(ZX_STREAM_MODE_READ | ZX_STREAM_MODE_WRITE, vmo, 0u, &stream);
  if (status != ZX_OK) {
    return ERROR(status);
  }

  zx::result io = fdio_internal::remote::create(std::move(vmo), std::move(stream));
  if (io.is_error()) {
    return io.status_value();
  }

  std::optional fd = bind_to_fd(io.value());
  if (fd.has_value()) {
    return fd.value();
  }
  return ERRNO(EMFILE);
}
