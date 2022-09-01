// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/lib/grpc/grpc_vsock_stub.h"

#include <lib/fpromise/bridge.h>
#include <lib/syslog/cpp/macros.h>

#include "src/virtualization/lib/grpc/fdio_util.h"

fpromise::promise<zx::socket, zx_status_t> ConnectToGrpcVsockService(
    const fuchsia::virtualization::HostVsockEndpointPtr& socket_endpoint, uint32_t port) {
  // Establish connection, hand first socket endpoint over to the guest.
  fpromise::bridge<zx::socket, zx_status_t> bridge;
  socket_endpoint->Connect(
      port, [completer = std::move(bridge.completer)](
                fuchsia::virtualization::HostVsockEndpoint_Connect_Result result) mutable {
        if (result.is_err()) {
          FX_LOGS(ERROR) << "Failed to connect: " << result.err();
          completer.complete_error(result.err());
        } else {
          completer.complete_ok(std::move(result.response().socket));
        }
      });
  return bridge.consumer.promise();
}
