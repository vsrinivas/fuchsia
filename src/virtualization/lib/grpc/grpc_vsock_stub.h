// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_LIB_GRPC_GRPC_VSOCK_STUB_H_
#define SRC_VIRTUALIZATION_LIB_GRPC_GRPC_VSOCK_STUB_H_

#include <fuchsia/virtualization/cpp/fidl.h>
#include <grpc++/grpc++.h>
#include <lib/fit/promise.h>

#include "src/virtualization/lib/grpc/fdio_util.h"

// Connects to a gRPC service listening on cid:port and returns a file
// descriptor for the connection socket.
//
// If you need to dispatch RPCs to the service, consider using
// |NewGrpcVsockStub| instead.
fit::promise<zx::socket, zx_status_t> ConnectToGrpcVsockService(
    const fuchsia::virtualization::HostVsockEndpointPtr& socket_endpoint,
    uint32_t cid, uint32_t port);

// Creates a new gRPC stub backed by a zx::socket.
template <typename T>
fit::result<std::unique_ptr<typename T::Stub>, zx_status_t> NewGrpcStub(
    zx::socket socket) {
  auto fd = ConvertSocketToNonBlockingFd(std::move(socket));
  if (fd < 0) {
    return fit::error(ZX_ERR_IO);
  }
  return fit::ok(T::NewStub(grpc::CreateInsecureChannelFromFd("vsock", fd)));
}

// Connects to a gRPC service listening on cid:port and returns a gRPC
// interface stub for the connection. This stub can be used to dispatch RPCs
// to the server.
template <typename T>
fit::promise<std::unique_ptr<typename T::Stub>, zx_status_t> NewGrpcVsockStub(
    const fuchsia::virtualization::HostVsockEndpointPtr& socket_endpoint,
    uint32_t cid, uint32_t port) {
  return ConnectToGrpcVsockService(socket_endpoint, cid, port)
      .and_then([](zx::socket& socket) mutable {
        return NewGrpcStub<T>(std::move(socket));
      });
}

#endif  // SRC_VIRTUALIZATION_LIB_GRPC_GRPC_VSOCK_STUB_H_
