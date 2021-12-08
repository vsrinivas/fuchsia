// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_CONTROLLER_PROVIDER_H_
#define SRC_SYS_FUZZING_COMMON_CONTROLLER_PROVIDER_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/fidl/cpp/interface_request.h>

#include <memory>
#include <string>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/binding.h"
#include "src/sys/fuzzing/common/controller.h"
#include "src/sys/fuzzing/common/dispatcher.h"
#include "src/sys/fuzzing/common/run-once.h"

namespace fuzzing {

using ::fuchsia::fuzzer::Controller;
using ::fuchsia::fuzzer::ControllerProvider;
using ::fuchsia::fuzzer::RegistrarSyncPtr;

class ControllerProviderImpl final : public ControllerProvider {
 public:
  ControllerProviderImpl();
  ~ControllerProviderImpl();

  // FIDL methods.
  void Connect(fidl::InterfaceRequest<Controller> request, ConnectCallback callback) override;
  void Stop() override;

  void SetRunner(std::unique_ptr<Runner> runner);

  // Fulfills requests received on the |channel| to connect to the |Controller|.
  void Serve(zx::channel channel);

  // Takes the startup channel provided by the fuzz_test_runner and serves
  // |fuchsia.fuzzer.ControllerProvider| on it. Blocks until the registry calls |Stop| and/or closes
  // the channel.
  zx_status_t Run(std::unique_ptr<Runner> runner);

 private:
  // Stop-related methods.
  void CloseImpl();
  void InterruptImpl();
  void JoinImpl();

  Binding<ControllerProvider> binding_;
  ControllerImpl controller_;
  RegistrarSyncPtr registrar_;

  RunOnce close_;
  RunOnce interrupt_;
  RunOnce join_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(ControllerProviderImpl);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_CONTROLLER_PROVIDER_H_
