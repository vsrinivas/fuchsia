// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/vmo.h>
#include <sys/mman.h>

#include <algorithm>

#include "fdio_unistd.h"
#include "internal.h"

// TODO(60236): remove declaration macros when symbol becomes available in libc
// headers.
__BEGIN_CDECLS

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

  fdio_t* io = nullptr;
  if ((io = fdio_vmo_create(std::move(vmo), std::move(stream))) == nullptr) {
    return ERROR(ZX_ERR_NO_MEMORY);
  }

  int fd = fdio_bind_to_fd(io, -1, 0);
  if (fd < 0) {
    fdio_release(io);
  }
  return fd;
}

__END_CDECLS
