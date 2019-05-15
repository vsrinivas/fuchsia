// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/lib/grpc/grpc_vsock_server.h"

#include <lib/fit/bridge.h>

#include "src/lib/fxl/logging.h"
#include "src/virtualization/lib/grpc/fdio_util.h"

void GrpcVsockServerBuilder::RegisterService(grpc::Service* service) {
  builder_->RegisterService(service);
}

void GrpcVsockServerBuilder::AddListenPort(uint32_t vsock_port) {
  fit::bridge<void, zx_status_t> bridge;
  (*socket_endpoint_)
      ->Listen(vsock_port, server_->NewBinding(),
               // The unused capture of |socket_endpoint_| here is important.
               // This is a shared_ptr we need to keep alive longer than this
               // closure so ensure the FIDL reply will be delivered. If we
               // don't do this, the underlying channel may be closed if the
               // builder is free'd after a call to Build().
               [socket_endpoint = socket_endpoint_,
                completer =
                    std::move(bridge.completer)](zx_status_t status) mutable {
                 if (status != ZX_OK) {
                   completer.complete_error(status);
                 } else {
                   completer.complete_ok();
                 }
               });
  service_promises_.push_back(bridge.consumer.promise());
}

fit::promise<std::unique_ptr<GrpcVsockServer>, zx_status_t>
GrpcVsockServerBuilder::Build() {
  return fit::join_promise_vector(std::move(service_promises_))
      .then([builder = std::move(builder_)](
                const fit::result<std::vector<fit::result<void, zx_status_t>>>&
                    result) mutable
            -> fit::result<std::unique_ptr<grpc::Server>, zx_status_t> {
        // join_promise_vector should never fail, but instead return a vector
        // of results.
        FXL_CHECK(result.is_ok())
            << "fit::join_promise_vector returns fit::error";
        for (const auto& result : result.value()) {
          if (result.is_error()) {
            FXL_CHECK(false)
                << "Failed to listen on vsock port: " << result.error();
            return fit::error(result.error());
          }
        }
        // All the vsock listeners have been initialized. Now start the gRPC
        // server.
        return fit::ok(builder->BuildAndStart());
      })
      .and_then([server = std::move(server_)](
                    std::unique_ptr<grpc::Server>& server_impl) mutable {
        server->SetServerImpl(std::move(server_impl));
        return fit::ok(std::move(server));
      });
}

// This method is registered as a FIDL callback for all of our vsock port
// listeners. In response we need to allocate a new zx::socket to use for the
// connection and register one end with gRPC.
void GrpcVsockServer::Accept(uint32_t src_cid, uint32_t src_port, uint32_t port,
                             AcceptCallback callback) {
  FXL_CHECK(server_);
  zx::socket h1, h2;
  zx_status_t status = zx::socket::create(ZX_SOCKET_STREAM, &h1, &h2);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create socket " << status;
    callback(ZX_ERR_CONNECTION_REFUSED, zx::handle());
    return;
  }

  // gRPC is not compatible with Zircon primitives, so we need to provide it
  // with a compatible file descriptor instead.
  int fd = ConvertSocketToNonBlockingFd(std::move(h1));
  if (fd < 0) {
    FXL_LOG(ERROR) << "Failed get file descriptor for socket";
    callback(ZX_ERR_INTERNAL, zx::socket());
    return;
  }
  grpc::AddInsecureChannelFromFd(server_.get(), fd);
  callback(ZX_OK, std::move(h2));
}
