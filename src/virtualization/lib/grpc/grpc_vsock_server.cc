// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/lib/grpc/grpc_vsock_server.h"

#include <lib/fdio/fd.h>
#include <lib/fpromise/bridge.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include "src/virtualization/lib/grpc/fdio_util.h"

void GrpcVsockServerBuilder::RegisterService(grpc::Service* service) {
  builder_->RegisterService(service);
}

void GrpcVsockServerBuilder::AddListenPort(uint32_t vsock_port) {
  fpromise::bridge<void, zx_status_t> bridge;
  (*socket_endpoint_)
      ->Listen(vsock_port, server_->NewBinding(),
               // The unused capture of |socket_endpoint_| here is important.
               // This is a shared_ptr we need to keep alive longer than this
               // closure so ensure the FIDL reply will be delivered. If we
               // don't do this, the underlying channel may be closed if the
               // builder is free'd after a call to Build().
               [socket_endpoint = socket_endpoint_,
                completer = std::move(bridge.completer)](zx_status_t status) mutable {
                 if (status != ZX_OK) {
                   completer.complete_error(status);
                 } else {
                   completer.complete_ok();
                 }
               });
  service_promises_.push_back(bridge.consumer.promise());
}

fpromise::promise<std::unique_ptr<GrpcVsockServer>, zx_status_t> GrpcVsockServerBuilder::Build() {
  return fpromise::join_promise_vector(std::move(service_promises_))
      .then(
          [builder = std::move(builder_)](
              const fpromise::result<std::vector<fpromise::result<void, zx_status_t>>>&
                  results) mutable -> fpromise::result<std::unique_ptr<grpc::Server>, zx_status_t> {
            // join_promise_vector should never fail, but instead return a vector
            // of results.
            FX_CHECK(results.is_ok()) << "fpromise::join_promise_vector returns fpromise::error";
            for (const auto& result : results.value()) {
              if (result.is_error()) {
                FX_CHECK(false) << "Failed to listen on vsock port: " << result.error();
                return fpromise::error(result.error());
              }
            }
            // All the vsock listeners have been initialized. Now start the gRPC
            // server.
            return fpromise::ok(builder->BuildAndStart());
          })
      .and_then([server = std::move(server_)](std::unique_ptr<grpc::Server>& server_impl) mutable {
        server->SetServerImpl(std::move(server_impl));
        return fpromise::ok(std::move(server));
      });
}

// This method is registered as a FIDL callback for all of our vsock port
// listeners. In response we need to allocate a new zx::socket to use for the
// connection and register one end with gRPC.
void GrpcVsockServer::Accept(uint32_t src_cid, uint32_t src_port, uint32_t port,
                             AcceptCallback callback) {
  FX_CHECK(server_);
  zx::socket h1, h2;
  zx_status_t status = [&]() {
    zx_status_t status;
    if ((status = zx::socket::create(ZX_SOCKET_STREAM, &h1, &h2)) != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to create socket " << zx_status_get_string(status);
      return ZX_ERR_CONNECTION_REFUSED;
    }

    // gRPC is not compatible with Zircon primitives, so we need to provide it
    // with a compatible file descriptor instead.
    fbl::unique_fd fd;
    if ((status = fdio_fd_create(h1.release(), fd.reset_and_get_address())) != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to get file descriptor for socket " << zx_status_get_string(status);
      return ZX_ERR_INTERNAL;
    }
    int result = SetNonBlocking(fd);
    if (result != 0) {
      FX_LOGS(ERROR) << "Failed to set nonblocking " << strerror(result);
      return ZX_ERR_INTERNAL;
    }

    grpc::AddInsecureChannelFromFd(server_.get(), fd.release());
    return ZX_OK;
  }();
  callback(status, std::move(h2));
}
