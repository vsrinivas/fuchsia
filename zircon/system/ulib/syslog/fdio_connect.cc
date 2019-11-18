// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fdio_connect.h"

#include <fuchsia/logger/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/txn_header.h>
#include <lib/zx/channel.h>

#include <cstring>

zx::socket connect_to_logger() {
  zx::socket invalid;
  zx::channel logger, logger_request;
  if (zx::channel::create(0, &logger, &logger_request) != ZX_OK) {
    return invalid;
  }
  if (fdio_service_connect("/svc/fuchsia.logger.LogSink", logger_request.release()) != ZX_OK) {
    return invalid;
  }
  zx::socket local, remote;
  if (zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote) != ZX_OK) {
    return invalid;
  }
  fuchsia_logger_LogSinkConnectRequest req;
  memset(&req, 0, sizeof(req));
  fidl_init_txn_header(&req.hdr, 0, fuchsia_logger_LogSinkConnectGenOrdinal);
  req.socket = FIDL_HANDLE_PRESENT;
  zx_handle_t handles[1] = {remote.release()};
  if (logger.write(0, &req, sizeof(req), handles, 1) != ZX_OK) {
    close(handles[0]);
    return invalid;
  }
  return local;
}
