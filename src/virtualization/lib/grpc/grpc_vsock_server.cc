// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/lib/grpc/grpc_vsock_server.h"

#include <lib/fdio/fd.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <unordered_set>

#include "src/virtualization/lib/grpc/fdio_util.h"

using ::fuchsia::virtualization::HostVsockAcceptor_Accept_Response;
using ::fuchsia::virtualization::HostVsockAcceptor_Accept_Result;
using ::fuchsia::virtualization::Listener;

void GrpcVsockServerBuilder::RegisterService(grpc::Service* service) {
  builder_->RegisterService(service);
}

void GrpcVsockServerBuilder::AddListenPort(uint32_t vsock_port) {
  listeners_.push_back({vsock_port, server_->NewBinding()});
}

zx::result<std::pair<std::unique_ptr<GrpcVsockServer>, std::vector<Listener>>>
GrpcVsockServerBuilder::Build() {
  if (listeners_.size() > 1) {
    std::unordered_set<uint32_t> ports;
    for (auto& listener : listeners_) {
      if (!ports.insert(listener.port).second) {
        return zx::error(ZX_ERR_ALREADY_BOUND);
      }
    }
  }

  server_->SetServerImpl(builder_->BuildAndStart());
  return zx::ok(std::make_pair(std::move(server_), std::move(listeners_)));
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

  callback(status == ZX_OK ? HostVsockAcceptor_Accept_Result::WithResponse(
                                 HostVsockAcceptor_Accept_Response(std::move(h2)))
                           : HostVsockAcceptor_Accept_Result::WithErr(std::move(status)));
}
