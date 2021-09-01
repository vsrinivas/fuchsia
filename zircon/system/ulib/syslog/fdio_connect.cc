// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fdio_connect.h"

#include <fidl/fuchsia.logger/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>
#include <unistd.h>

#include "fx_logger.h"

zx::socket connect_to_logger() {
  zx::socket invalid;
  zx::channel logger, logger_request;
  if (zx::channel::create(0, &logger, &logger_request) != ZX_OK) {
    return invalid;
  }
  fidl::WireSyncClient<fuchsia_logger::LogSink> logger_client(std::move(logger));

  if (fdio_service_connect("/svc/fuchsia.logger.LogSink", logger_request.release()) != ZX_OK) {
    return invalid;
  }
  zx::socket local, remote;
  if (zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote) != ZX_OK) {
    return invalid;
  }
  if (syslog_backend::HasStructuredBackend()) {
    auto result = logger_client.ConnectStructured(std::move(remote));
    if (result.status() != ZX_OK) {
      return invalid;
    }
  } else {
    auto result = logger_client.Connect(std::move(remote));
    if (result.status() != ZX_OK) {
      return invalid;
    }
  }
  return local;
}
