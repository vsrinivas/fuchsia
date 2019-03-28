// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_PKG_BISCOTTI_GUEST_LINUX_RUNNER_GUEST_H_
#define GARNET_BIN_GUEST_PKG_BISCOTTI_GUEST_LINUX_RUNNER_GUEST_H_

#include <zircon/types.h>
#include <deque>
#include <memory>

#include <fuchsia/guest/cpp/fidl.h>
#include <grpc++/grpc++.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fxl/command_line.h>
#include <lib/guest/scenic_wayland_dispatcher.h>

#include "garnet/bin/guest/pkg/biscotti_guest/linux_runner/linux_component.h"
#include "garnet/bin/guest/pkg/biscotti_guest/linux_runner/log_collector.h"
#include "garnet/bin/guest/pkg/biscotti_guest/third_party/protos/container_guest.grpc.pb.h"
#include "garnet/bin/guest/pkg/biscotti_guest/third_party/protos/container_host.grpc.pb.h"
#include "garnet/bin/guest/pkg/biscotti_guest/third_party/protos/tremplin.grpc.pb.h"
#include "garnet/bin/guest/pkg/biscotti_guest/third_party/protos/vm_guest.grpc.pb.h"

namespace linux_runner {

struct AppLaunchRequest {
 public:
  AppLaunchRequest(const AppLaunchRequest&) = delete;
  AppLaunchRequest& operator=(const AppLaunchRequest&) = delete;

  AppLaunchRequest(AppLaunchRequest&&) = default;
  AppLaunchRequest& operator=(AppLaunchRequest&&) = default;

  fuchsia::sys::Package application;
  fuchsia::sys::StartupInfo startup_info;
  fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller_request;
};

class Guest : public fuchsia::guest::HostVsockAcceptor,
              public vm_tools::StartupListener::Service,
              public vm_tools::tremplin::TremplinListener::Service,
              public vm_tools::container::ContainerListener::Service {
 public:
  // Creates a new |Guest|
  static zx_status_t CreateAndStart(component::StartupContext* context,
                                    fxl::CommandLine cl,
                                    std::unique_ptr<Guest>* guest);

  Guest(component::StartupContext* context,
        fuchsia::guest::EnvironmentControllerPtr env, fxl::CommandLine cl);

  void Launch(AppLaunchRequest request);

 private:
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
  void DumpContainerDebugInfo();

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

  // |vm_tools::container::ContainerListener::Service|
  grpc::Status ContainerReady(
      grpc::ServerContext* context,
      const vm_tools::container::ContainerStartupInfo* request,
      vm_tools::EmptyMessage* response) override;
  grpc::Status ContainerShutdown(
      grpc::ServerContext* context,
      const vm_tools::container::ContainerShutdownInfo* request,
      vm_tools::EmptyMessage* response) override;
  grpc::Status UpdateApplicationList(
      grpc::ServerContext* context,
      const vm_tools::container::UpdateApplicationListRequest* request,
      vm_tools::EmptyMessage* response) override;
  grpc::Status OpenUrl(grpc::ServerContext* context,
                       const vm_tools::container::OpenUrlRequest* request,
                       vm_tools::EmptyMessage* response) override;
  grpc::Status InstallLinuxPackageProgress(
      grpc::ServerContext* context,
      const vm_tools::container::InstallLinuxPackageProgressInfo* request,
      vm_tools::EmptyMessage* response) override;
  grpc::Status UninstallPackageProgress(
      grpc::ServerContext* context,
      const vm_tools::container::UninstallPackageProgressInfo* request,
      vm_tools::EmptyMessage* response) override;
  grpc::Status OpenTerminal(
      grpc::ServerContext* context,
      const vm_tools::container::OpenTerminalRequest* request,
      vm_tools::EmptyMessage* response) override;
  grpc::Status UpdateMimeTypes(
      grpc::ServerContext* context,
      const vm_tools::container::UpdateMimeTypesRequest* request,
      vm_tools::EmptyMessage* response) override;

  template <typename Service>
  std::unique_ptr<typename Service::Stub> NewVsockStub(uint32_t cid,
                                                       uint32_t port);
  void LaunchApplication(AppLaunchRequest request);
  void OnNewView(fidl::InterfaceHandle<fuchsia::ui::app::ViewProvider> view);
  void CreateComponent(
      AppLaunchRequest request,
      fidl::InterfaceHandle<fuchsia::ui::app::ViewProvider> view);
  void OnComponentTerminated(const LinuxComponent* component);

  async_dispatcher_t* async_;
  std::unique_ptr<grpc::Server> grpc_server_;
  fuchsia::guest::HostVsockEndpointSyncPtr socket_endpoint_;
  fidl::BindingSet<fuchsia::guest::HostVsockAcceptor> acceptor_bindings_;
  fuchsia::guest::EnvironmentControllerPtr guest_env_;
  fuchsia::guest::InstanceControllerPtr guest_controller_;
  uint32_t guest_cid_ = 0;
  std::unique_ptr<vm_tools::Maitred::Stub> maitred_;
  std::unique_ptr<vm_tools::tremplin::Tremplin::Stub> tremplin_;
  std::unique_ptr<vm_tools::container::Garcon::Stub> garcon_;
  LogCollector log_collector_;
  fxl::CommandLine cl_;
  guest::ScenicWaylandDispatcher wayland_dispatcher_;
  // Requests queued up waiting for the guest to fully boot.
  std::deque<AppLaunchRequest> pending_requests_;
  // Requests that have been dispatched to the container, but have not yet been
  // associated with a wayland ViewProvider.
  std::deque<AppLaunchRequest> pending_views_;
  // Views launched in the background (ex: not using garcon). These can be
  // returned by requesting a null app URI (linux://).
  std::deque<fidl::InterfaceHandle<fuchsia::ui::app::ViewProvider>>
      background_views_;
  std::unordered_map<const LinuxComponent*, std::unique_ptr<LinuxComponent>>
      components_;
};
}  // namespace linux_runner

#endif  // GARNET_BIN_GUEST_PKG_BISCOTTI_GUEST_LINUX_RUNNER_GUEST_H_
