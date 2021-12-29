// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_CLIENT_GUEST_INTERACTION_SERVICE_H_
#define SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_CLIENT_GUEST_INTERACTION_SERVICE_H_

#include <fuchsia/netemul/guest/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>
#include <threads.h>

#include "src/lib/fxl/macros.h"
#include "src/virtualization/lib/guest_interaction/client/client_impl.h"
#include "src/virtualization/lib/guest_interaction/platform_interface/platform_interface.h"

class FuchsiaGuestInteractionService final : public fuchsia::netemul::guest::GuestInteraction {
 public:
  FuchsiaGuestInteractionService(zx::socket socket, async_dispatcher_t* dispatcher);
  ~FuchsiaGuestInteractionService() override;
  void PutFile(fidl::InterfaceHandle<fuchsia::io::File> local_file, std::string remote_path,
               fit::function<void(zx_status_t)> callback) override;
  void GetFile(std::string remote_path, fidl::InterfaceHandle<fuchsia::io::File> local_file,
               fit::function<void(zx_status_t)> callback) override;
  void ExecuteCommand(
      std::string command, std::vector<fuchsia::netemul::guest::EnvironmentVariable> env,
      zx::socket std_in, zx::socket std_out, zx::socket std_err,
      fidl::InterfaceRequest<fuchsia::netemul::guest::CommandListener> req) override;
  void AddBinding(fidl::InterfaceRequest<fuchsia::netemul::guest::GuestInteraction> request);

 private:
  ClientImpl<PosixPlatform> client_;
  async_dispatcher_t* dispatcher_;
  fidl::BindingSet<fuchsia::netemul::guest::GuestInteraction> bindings_;
  thrd_t guest_interaction_service_thread_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(FuchsiaGuestInteractionService);
};

#endif  // SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_CLIENT_GUEST_INTERACTION_SERVICE_H_
