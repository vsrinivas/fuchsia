// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_RUNNER_GUEST_RUNNER_H_
#define GARNET_BIN_GUEST_RUNNER_GUEST_RUNNER_H_

#include <component/cpp/fidl.h>

#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/macros.h"

namespace guest_runner {

class GuestRunner : public component::Runner {
 public:
  explicit GuestRunner();
  ~GuestRunner() override;

 private:
  // |component::Runner|
  void StartComponent(component::Package application,
                      component::StartupInfo startup_info,
                      ::fidl::InterfaceRequest<component::ComponentController>
                          controller) override;

  component::ApplicationLauncherSyncPtr launcher_;
  std::unique_ptr<component::ApplicationContext> context_;
  fidl::BindingSet<component::Runner> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GuestRunner);
};

}  // namespace guest_runner

#endif  // GARNET_BIN_GUEST_RUNNER_GUEST_RUNNER_H_
