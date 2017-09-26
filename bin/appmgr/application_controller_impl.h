// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_APPLICATION_CONTROLLER_IMPL_H_
#define GARNET_BIN_APPMGR_APPLICATION_CONTROLLER_IMPL_H_

#include <zx/process.h>

#include "garnet/lib/farfs/file_system.h"
#include "lib/app/fidl/application_controller.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/macros.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/tasks/message_loop_handler.h"

namespace app {
class ApplicationEnvironmentImpl;

class ApplicationControllerImpl : public ApplicationController,
                                  public fsl::MessageLoopHandler {
 public:
  ApplicationControllerImpl(
      fidl::InterfaceRequest<ApplicationController> request,
      ApplicationEnvironmentImpl* environment,
      std::unique_ptr<archive::FileSystem> fs,
      zx::process process,
      std::string path);
  ~ApplicationControllerImpl() override;

  const std::string& path() const { return path_; }

  // |ApplicationController| implementation:
  void Kill() override;
  void Detach() override;
  void Wait(const WaitCallback& callback) override;

 private:
  // |fsl::MessageLoopHandler| implementation:
  void OnHandleReady(zx_handle_t handle, zx_signals_t pending, uint64_t count) override;

  bool SendReturnCodeIfTerminated();

  fidl::Binding<ApplicationController> binding_;
  ApplicationEnvironmentImpl* environment_;
  std::unique_ptr<archive::FileSystem> fs_;
  zx::process process_;
  std::string path_;
  std::vector<WaitCallback> wait_callbacks_;

  fsl::MessageLoop::HandlerKey termination_handler_ = 0u;

  FXL_DISALLOW_COPY_AND_ASSIGN(ApplicationControllerImpl);
};

}  // namespace app

#endif  // GARNET_BIN_APPMGR_APPLICATION_CONTROLLER_IMPL_H_
