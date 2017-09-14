// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fsl/io/redirection.h"

#include <zircon/processargs.h>

#include <utility>

namespace fsl {

zx_status_t CreateRedirectedSocket(int startup_fd,
                                   zx::socket* out_socket,
                                   StartupHandle* out_startup_handle) {
  zx::socket local_socket;
  zx::socket remote_socket;
  zx_status_t status = zx::socket::create(0u, &local_socket, &remote_socket);
  if (status != ZX_OK)
    return status;

  *out_socket = std::move(local_socket);
  out_startup_handle->id = PA_HND(PA_FDIO_PIPE, startup_fd);
  out_startup_handle->handle = std::move(remote_socket);
  return ZX_OK;
}

}  // namespace fsl
