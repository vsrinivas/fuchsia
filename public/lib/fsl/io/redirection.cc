// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fsl/io/redirection.h"

#include <magenta/processargs.h>

#include <utility>

namespace fsl {

mx_status_t CreateRedirectedSocket(int startup_fd,
                                   mx::socket* out_socket,
                                   StartupHandle* out_startup_handle) {
  mx::socket local_socket;
  mx::socket remote_socket;
  mx_status_t status = mx::socket::create(0u, &local_socket, &remote_socket);
  if (status != MX_OK)
    return status;

  *out_socket = std::move(local_socket);
  out_startup_handle->id = PA_HND(PA_MXIO_PIPE, startup_fd);
  out_startup_handle->handle = std::move(remote_socket);
  return MX_OK;
}

}  // namespace fsl
