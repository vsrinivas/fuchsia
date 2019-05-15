// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_LIB_GRPC_GRPC_VSOCK_SERVER_H_
#define SRC_VIRTUALIZATION_LIB_GRPC_GRPC_VSOCK_SERVER_H_

#include <fuchsia/virtualization/cpp/fidl.h>
#include <grpc++/grpc++.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fit/promise.h>

// A thin wrapper around |grpc::Server| that handles accepting connections
// from a |fuchsia::virtualization::HostVsockEndpoint| and adding them to a
// |grpc::Server|.
//
// This class cannot be instanitated directly, but an instance can be created
// using the |GrpcVsockServerBuilder|.
class GrpcVsockServer : public fuchsia::virtualization::HostVsockAcceptor {
 public:
  // Gets a pointer to the underlying server.
  grpc::Server* inner() { return server_.get(); }

 protected:
  friend class GrpcVsockServerBuilder;

  fidl::InterfaceHandle<fuchsia::virtualization::HostVsockAcceptor>
  NewBinding() {
    return bindings_.AddBinding(this);
  }

  void SetServerImpl(std::unique_ptr<grpc::Server> server) {
    server_ = std::move(server);
  }

 private:
  // |fuchsia::virtualization::HostVsockAcceptor|
  void Accept(uint32_t src_cid, uint32_t src_port, uint32_t port,
              AcceptCallback callback) override;

  fidl::BindingSet<fuchsia::virtualization::HostVsockAcceptor> bindings_;
  std::unique_ptr<grpc::Server> server_;
};

// A thin wrapper around |grpc::ServerBuilder| that also registers the service
// ports with the |HostVsockEndpoint|.
class GrpcVsockServerBuilder {
 public:
  GrpcVsockServerBuilder(
      fuchsia::virtualization::HostVsockEndpointPtr socket_endpoint)
      : socket_endpoint_(
            std::make_shared<fuchsia::virtualization::HostVsockEndpointPtr>(
                std::move(socket_endpoint))),
        builder_(new grpc::ServerBuilder()),
        server_(new GrpcVsockServer()) {}

  // Registers the gRPC service.
  //
  // You must add one or more vsock ports with |AddListenPort| for this service
  // to be accessible over vsock.
  void RegisterService(grpc::Service* service);

  // Listens on |vsock_port| for new, in-bound connections.
  //
  // All services added with |RegisterService| will be made available on this
  // port.
  void AddListenPort(uint32_t vsock_port);

  // Constructs the |GrpcVsockServer| and starts processing any in-bound
  // requests on the sockets.
  //
  // It is safe to free the builder immediately after a call to |Build|.
  fit::promise<std::unique_ptr<GrpcVsockServer>, zx_status_t> Build();

 private:
  std::vector<fit::promise<void, zx_status_t>> service_promises_;
  std::shared_ptr<fuchsia::virtualization::HostVsockEndpointPtr>
      socket_endpoint_;
  std::unique_ptr<grpc::ServerBuilder> builder_;
  std::unique_ptr<GrpcVsockServer> server_;
};

#endif  // SRC_VIRTUALIZATION_LIB_GRPC_GRPC_VSOCK_SERVER_H_
