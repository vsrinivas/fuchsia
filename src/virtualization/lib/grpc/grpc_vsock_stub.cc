// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/lib/grpc/grpc_vsock_stub.h"

#include <lib/fit/bridge.h>

#include "src/lib/fxl/logging.h"
#include "src/virtualization/lib/grpc/fdio_util.h"

fit::promise<zx::socket, zx_status_t> ConnectToGrpcVsockService(
    const fuchsia::virtualization::HostVsockEndpointPtr& socket_endpoint,
    uint32_t cid, uint32_t port) {
  // Create the socket for the connection.
  zx::socket h1, h2;
  zx_status_t status = zx::socket::create(ZX_SOCKET_STREAM, &h1, &h2);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create socket";
    return fit::make_result_promise<zx::socket, zx_status_t>(
        fit::error(status));
  }

  // Establish connection, hand first socket endpoint over to the guest.
  fit::bridge<zx::socket, zx_status_t> bridge;
  socket_endpoint->Connect(cid, port, std::move(h1),
                           [completer = std::move(bridge.completer),
                            h2 = std::move(h2)](zx_status_t status) mutable {
                             if (status != ZX_OK) {
                               FXL_LOG(ERROR)
                                   << "Failed to connect: " << status;
                               completer.complete_error(status);
                             } else {
                               completer.complete_ok(std::move(h2));
                             }
                           });
  return bridge.consumer.promise();
}
