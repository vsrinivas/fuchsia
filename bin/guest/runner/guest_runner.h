// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_RUNNER_GUEST_RUNNER_H_
#define GARNET_BIN_GUEST_RUNNER_GUEST_RUNNER_H_

#include <fuchsia/sys/cpp/fidl.h>

#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/macros.h"

namespace guest_runner {

class GuestRunner : public fuchsia::sys::Runner {
 public:
  explicit GuestRunner();
  ~GuestRunner() override;

 private:
  // |fuchsia::sys::Runner|
  void StartComponent(
      fuchsia::sys::Package application, fuchsia::sys::StartupInfo startup_info,
      ::fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller)
      override;

  fuchsia::sys::LauncherSyncPtr launcher_;
  std::unique_ptr<component::StartupContext> context_;
  fidl::BindingSet<fuchsia::sys::Runner> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GuestRunner);
};

}  // namespace guest_runner

#endif  // GARNET_BIN_GUEST_RUNNER_GUEST_RUNNER_H_
