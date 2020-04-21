// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fdio_connect.h"

#include <fuchsia/logger/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>

zx::socket connect_to_logger() {
  zx::socket invalid;
  zx::channel logger, logger_request;
  if (zx::channel::create(0, &logger, &logger_request) != ZX_OK) {
    return invalid;
  }
  ::llcpp::fuchsia::logger::LogSink::SyncClient logger_client(std::move(logger));

  if (fdio_service_connect("/svc/fuchsia.logger.LogSink", logger_request.release()) != ZX_OK) {
    return invalid;
  }
  zx::socket local, remote;
  if (zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote) != ZX_OK) {
    return invalid;
  }

  auto result = logger_client.Connect(std::move(remote));
  if (result.status() != ZX_OK) {
    return invalid;
  }
  return local;
}
