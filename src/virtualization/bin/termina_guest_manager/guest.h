// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_TERMINA_GUEST_MANAGER_GUEST_H_
#define SRC_VIRTUALIZATION_BIN_TERMINA_GUEST_MANAGER_GUEST_H_

#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <zircon/types.h>

#include <deque>
#include <memory>

#include "src/virtualization/bin/termina_guest_manager/crash_listener.h"
#include "src/virtualization/bin/termina_guest_manager/log_collector.h"
#include "src/virtualization/bin/termina_guest_manager/termina_config.h"
#include "src/virtualization/lib/grpc/grpc_vsock_server.h"
#include "src/virtualization/third_party/vm_tools/container_guest.grpc.pb.h"
#include "src/virtualization/third_party/vm_tools/container_host.grpc.pb.h"
#include "src/virtualization/third_party/vm_tools/tremplin.grpc.pb.h"
#include "src/virtualization/third_party/vm_tools/vm_guest.grpc.pb.h"

#include <grpc++/grpc++.h>

namespace termina_guest_manager {

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
  Guest(const termina_config::Config& config, GuestInfoCallback callback);

  ~Guest();

  // Retry the container startup workflow.
  //
  // Creating the container can fail under expected situations (ex: no network connectivity) so
  // we provide a way to retry that workflow.
  //
  // This must only be used in response to a previous failure to start the container, which is
  // indicated by a ContainerStatus::FAILED message sent to the GuestInfoCallback.
  void RetryContainerStartup() { CreateContainer(); }

  void OnGuestLaunched(fuchsia::virtualization::GuestManager& guest_manager,
                       fuchsia::virtualization::Guest& guest);

  // Send a shutdown RPC to the guest VM.
  void InitiateGuestShutdown();

  std::vector<fuchsia::virtualization::Listener> take_vsock_listeners() {
    FX_CHECK(vsock_listeners_);
    std::optional<std::vector<fuchsia::virtualization::Listener>> result = std::nullopt;
    vsock_listeners_.swap(result);
    return std::move(*result);
  }

 private:
  zx::status<> StartGrpcServer();
  void StartGuest(fuchsia::virtualization::GuestManager& guest_manager);
  void MountFilesystems();
  void MountReadOnlyFilesystem(const std::string& source, const std::string& target,
                               const std::string& fstype);
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

  async::Executor executor_;
  GuestInfoCallback callback_;
  termina_config::Config structured_config_;
  std::unique_ptr<GrpcVsockServer> grpc_server_;
  fuchsia::virtualization::HostVsockEndpointPtr socket_endpoint_;
  uint32_t guest_cid_ = fuchsia::virtualization::DEFAULT_GUEST_CID;
  std::unique_ptr<vm_tools::Maitred::Stub> maitred_;
  std::unique_ptr<vm_tools::tremplin::Tremplin::Stub> tremplin_;
  std::unique_ptr<vm_tools::container::Garcon::Stub> garcon_;
  CrashListener crash_listener_;
  LogCollector log_collector_;
  std::optional<std::vector<::fuchsia::virtualization::Listener>> vsock_listeners_;

  // A flow ID used to track the time from the time the VM is created until
  // the time the guest has reported itself as ready via the VmReady RPC in the
  // vm_tools::StartupListener::Service.
  const trace_async_id_t vm_ready_nonce_ = TRACE_NONCE();

  // Set to true if a shutdown was attempted before a maitre'd connection was established.
  bool must_send_shutdown_rpc_ = false;
};
}  // namespace termina_guest_manager

#endif  // SRC_VIRTUALIZATION_BIN_TERMINA_GUEST_MANAGER_GUEST_H_
