// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_PKG_BISCOTTI_GUEST_BIN_GUEST_H_
#define GARNET_BIN_GUEST_PKG_BISCOTTI_GUEST_BIN_GUEST_H_

#include <memory>

#include <fuchsia/guest/cpp/fidl.h>
#include <grpc++/grpc++.h>
#include <zircon/types.h>

#include "garnet/bin/guest/pkg/biscotti_guest/bin/log_collector.h"
#include "garnet/bin/guest/pkg/biscotti_guest/third_party/protos/tremplin.grpc.pb.h"
#include "garnet/bin/guest/pkg/biscotti_guest/third_party/protos/vm_guest.grpc.pb.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/binding_set.h"

namespace biscotti {
class Guest : public fuchsia::guest::HostVsockAcceptor,
              public vm_tools::StartupListener::Service,
              public vm_tools::tremplin::TremplinListener::Service {
 public:
  // Creates a new |Guest|
  static zx_status_t CreateAndStart(component::StartupContext* context,
                                    std::unique_ptr<Guest>* guest);

 private:
  Guest(fuchsia::guest::EnvironmentControllerPtr env);

  void Start();
  void StartGrpcServer();
  void StartGuest();
  void ConfigureNetwork();
  void StartTermina();
  void LaunchVmShell();
  void LaunchContainerShell();
  void CreateContainer();
  void StartContainer();
  void SetupUser();

  // |fuchsia::guest::HostVsockAcceptor|
  void Accept(uint32_t src_cid, uint32_t src_port, uint32_t port,
              AcceptCallback callback) override;

  // |vm_tools::StartupListener::Service|
  grpc::Status VmReady(grpc::ServerContext* context,
                       const vm_tools::EmptyMessage* request,
                       vm_tools::EmptyMessage* response) override;
  grpc::Status ContainerStartupFailed(
      grpc::ServerContext* context, const vm_tools::ContainerName* request,
      vm_tools::EmptyMessage* response) override;

  // |vm_tools::tremplin::TremplinListener::Service|
  grpc::Status TremplinReady(
      grpc::ServerContext* context,
      const ::vm_tools::tremplin::TremplinStartupInfo* request,
      vm_tools::tremplin::EmptyMessage* response) override;
  grpc::Status UpdateCreateStatus(
      grpc::ServerContext* context,
      const vm_tools::tremplin::ContainerCreationProgress* request,
      vm_tools::tremplin::EmptyMessage* response) override;

  template <typename Service>
  std::unique_ptr<typename Service::Stub> NewVsockStub(uint32_t cid,
                                                       uint32_t port);

  async_dispatcher_t* async_;
  std::unique_ptr<grpc::Server> grpc_server_;
  fuchsia::guest::HostVsockEndpointSyncPtr socket_endpoint_;
  fidl::BindingSet<fuchsia::guest::HostVsockAcceptor> acceptor_bindings_;
  fuchsia::guest::EnvironmentControllerPtr guest_env_;
  fuchsia::guest::InstanceControllerPtr guest_controller_;
  uint32_t guest_cid_ = 0;
  std::unique_ptr<vm_tools::Maitred::Stub> maitred_;
  std::unique_ptr<vm_tools::tremplin::Tremplin::Stub> tremplin_;
  LogCollector log_collector_;
};
}  // namespace biscotti

#endif  // GARNET_BIN_GUEST_PKG_BISCOTTI_GUEST_BIN_GUEST_H_
