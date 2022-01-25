// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_LINUX_RUNNER_GUEST_H_
#define SRC_VIRTUALIZATION_BIN_LINUX_RUNNER_GUEST_H_

#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/promise.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/trace/event.h>
#include <lib/virtualization/scenic_wayland_dispatcher.h>
#include <zircon/types.h>

#include <deque>
#include <memory>

#include "src/virtualization/bin/linux_runner/crash_listener.h"
#include "src/virtualization/bin/linux_runner/log_collector.h"
#include "src/virtualization/lib/grpc/grpc_vsock_server.h"
#include "src/virtualization/third_party/vm_tools/container_guest.grpc.pb.h"
#include "src/virtualization/third_party/vm_tools/container_host.grpc.pb.h"
#include "src/virtualization/third_party/vm_tools/tremplin.grpc.pb.h"
#include "src/virtualization/third_party/vm_tools/vm_guest.grpc.pb.h"

#include <grpc++/grpc++.h>

namespace linux_runner {

struct GuestConfig {
  std::string_view env_label;
  size_t stateful_image_size;
};

struct GuestInfo {
  uint32_t cid;
  fuchsia::virtualization::ContainerStatus container_status;
  int32_t download_percent;
  std::string failure_reason;
};

using GuestInfoCallback = fit::function<void(GuestInfo)>;

class Guest : public vm_tools::StartupListener::Service,
              public vm_tools::tremplin::TremplinListener::Service,
              public vm_tools::container::ContainerListener::Service {
 public:
  // Creates a new |Guest|
  static zx_status_t CreateAndStart(sys::ComponentContext* context, GuestConfig config,
                                    GuestInfoCallback callback, std::unique_ptr<Guest>* guest);

  Guest(sys::ComponentContext* context, GuestConfig config, GuestInfoCallback callback,
        fuchsia::virtualization::RealmPtr env);
  ~Guest();

 private:
  fpromise::promise<> Start();
  fpromise::promise<std::unique_ptr<GrpcVsockServer>, zx_status_t> StartGrpcServer();
  std::vector<fuchsia::virtualization::BlockSpec> GetBlockDevices(size_t stateful_image_size);
  void StartGuest();
  void MountExtrasPartition();
  void MountVmTools();
  void ConfigureNetwork();
  void StartTermina();
  void LaunchContainerShell();
  void AddMagmaDeviceToContainer();
  void SetupGPUDriversInContainer();
  void CreateContainer();
  void StartContainer();
  void SetupUser();
  void DumpContainerDebugInfo();

  // |vm_tools::StartupListener::Service|
  grpc::Status VmReady(grpc::ServerContext* context, const vm_tools::EmptyMessage* request,
                       vm_tools::EmptyMessage* response) override;

  // |vm_tools::tremplin::TremplinListener::Service|
  grpc::Status TremplinReady(grpc::ServerContext* context,
                             const ::vm_tools::tremplin::TremplinStartupInfo* request,
                             vm_tools::tremplin::EmptyMessage* response) override;
  grpc::Status UpdateCreateStatus(grpc::ServerContext* context,
                                  const vm_tools::tremplin::ContainerCreationProgress* request,
                                  vm_tools::tremplin::EmptyMessage* response) override;
  grpc::Status UpdateDeletionStatus(::grpc::ServerContext* context,
                                    const ::vm_tools::tremplin::ContainerDeletionProgress* request,
                                    ::vm_tools::tremplin::EmptyMessage* response) override;
  grpc::Status UpdateStartStatus(::grpc::ServerContext* context,
                                 const ::vm_tools::tremplin::ContainerStartProgress* request,
                                 ::vm_tools::tremplin::EmptyMessage* response) override;
  grpc::Status UpdateExportStatus(::grpc::ServerContext* context,
                                  const ::vm_tools::tremplin::ContainerExportProgress* request,
                                  ::vm_tools::tremplin::EmptyMessage* response) override;
  grpc::Status UpdateImportStatus(::grpc::ServerContext* context,
                                  const ::vm_tools::tremplin::ContainerImportProgress* request,
                                  ::vm_tools::tremplin::EmptyMessage* response) override;
  grpc::Status ContainerShutdown(::grpc::ServerContext* context,
                                 const ::vm_tools::tremplin::ContainerShutdownInfo* request,
                                 ::vm_tools::tremplin::EmptyMessage* response) override;

  // |vm_tools::container::ContainerListener::Service|
  grpc::Status ContainerReady(grpc::ServerContext* context,
                              const vm_tools::container::ContainerStartupInfo* request,
                              vm_tools::EmptyMessage* response) override;
  grpc::Status ContainerShutdown(grpc::ServerContext* context,
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
  grpc::Status OpenTerminal(grpc::ServerContext* context,
                            const vm_tools::container::OpenTerminalRequest* request,
                            vm_tools::EmptyMessage* response) override;
  grpc::Status UpdateMimeTypes(grpc::ServerContext* context,
                               const vm_tools::container::UpdateMimeTypesRequest* request,
                               vm_tools::EmptyMessage* response) override;

  void PostContainerStatus(fuchsia::virtualization::ContainerStatus container_status);
  void PostContainerDownloadProgress(int32_t download_progress);
  void PostContainerFailure(std::string failure_reason);

  async_dispatcher_t* async_;
  async::Executor executor_;
  GuestConfig config_;
  GuestInfoCallback callback_;
  std::unique_ptr<GrpcVsockServer> grpc_server_;
  fuchsia::virtualization::HostVsockEndpointPtr socket_endpoint_;
  fuchsia::virtualization::RealmPtr guest_env_;
  fuchsia::virtualization::GuestPtr guest_controller_;
  uint32_t guest_cid_ = 0;
  std::unique_ptr<vm_tools::Maitred::Stub> maitred_;
  std::unique_ptr<vm_tools::tremplin::Tremplin::Stub> tremplin_;
  std::unique_ptr<vm_tools::container::Garcon::Stub> garcon_;
  CrashListener crash_listener_;
  LogCollector log_collector_;
  guest::ScenicWaylandDispatcher wayland_dispatcher_;
  fuchsia::sys::LauncherPtr launcher_;

  // A flow ID used to track the time from the time the VM is created until
  // the time the guest has reported itself as ready via the VmReady RPC in the
  // vm_tools::StartupListener::Service.
  const trace_async_id_t vm_ready_nonce_ = TRACE_NONCE();
};
}  // namespace linux_runner

#endif  // SRC_VIRTUALIZATION_BIN_LINUX_RUNNER_GUEST_H_
