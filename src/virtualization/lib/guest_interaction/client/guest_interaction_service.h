// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_CLIENT_GUEST_INTERACTION_SERVICE_H_
#define SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_CLIENT_GUEST_INTERACTION_SERVICE_H_

#include <fuchsia/io/cpp/fidl.h>
#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/promise.h>
#include <lib/fpromise/scope.h>
#include <lib/sys/cpp/component_context.h>
#include <threads.h>
#include <zircon/status.h>

#include <vector>

#include "fuchsia/netemul/guest/cpp/fidl.h"
#include "src/lib/fxl/macros.h"
#include "src/virtualization/lib/guest_interaction/client/client_impl.h"
#include "src/virtualization/lib/guest_interaction/platform_interface/platform_interface.h"
#include "src/virtualization/lib/guest_interaction/proto/guest_interaction.grpc.pb.h"

class FuchsiaGuestInteractionService final : public fuchsia::netemul::guest::GuestInteraction {
 public:
  explicit FuchsiaGuestInteractionService(zx::socket socket);
  ~FuchsiaGuestInteractionService();
  void PutFile(fidl::InterfaceHandle<fuchsia::io::File> local_file, std::string remote_path,
               fit::function<void(zx_status_t)> callback) override;
  void GetFile(std::string remote_path, fidl::InterfaceHandle<fuchsia::io::File> local_file,
               fit::function<void(zx_status_t)> callback) override;
  void ExecuteCommand(
      std::string command, std::vector<fuchsia::netemul::guest::EnvironmentVariable> env,
      zx::socket std_in, zx::socket std_out, zx::socket std_err,
      fidl::InterfaceRequest<fuchsia::netemul::guest::CommandListener> req) override;
  void AddBinding(fidl::InterfaceRequest<fuchsia::netemul::guest::GuestInteraction> request);
  void Run();
  void Stop();

 private:
  int32_t GetVsockFd(std::string vm_label);
  fpromise::promise<zx_status_t> InitiatePut(zx::channel local_file,
                                             const std::string& remote_path);
  fpromise::promise<zx_status_t> InitiateGet(const std::string& remote_path,
                                             zx::channel local_file);

  std::unique_ptr<ClientImpl<PosixPlatform>> client_;
  fidl::BindingSet<fuchsia::netemul::guest::GuestInteraction> bindings_;
  async::Executor executor_;
  fpromise::scope scope_;
  thrd_t guest_interaction_service_thread_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(FuchsiaGuestInteractionService);
};

#endif  // SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_CLIENT_GUEST_INTERACTION_SERVICE_H_
