// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_MANAGED_LAUNCHER_H_
#define SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_MANAGED_LAUNCHER_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <src/lib/fxl/macros.h>

#include <string>

#include "managed_logger.h"

namespace netemul {
class ManagedEnvironment;
class ManagedLauncher : public fuchsia::sys::Launcher {
 public:
  explicit ManagedLauncher(ManagedEnvironment* environment);

  ~ManagedLauncher();

  void CreateComponent(fuchsia::sys::LaunchInfo launch_info,
                       fidl::InterfaceRequest<fuchsia::sys::ComponentController>
                           controller) override;

 protected:
  friend ManagedEnvironment;
  void Bind(fidl::InterfaceRequest<fuchsia::sys::Launcher> request);

 private:
  void CreateComponent(
      fuchsia::sys::PackagePtr package, fuchsia::sys::LaunchInfo launch_info,
      fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller);

  // Pointer to parent environment. Not owned.
  ManagedEnvironment* env_;
  fuchsia::sys::LauncherPtr real_launcher_;
  fuchsia::sys::LoaderPtr loader_;
  fidl::BindingSet<fuchsia::sys::Launcher> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ManagedLauncher);
};

}  // namespace netemul

#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_MANAGED_LAUNCHER_H_
