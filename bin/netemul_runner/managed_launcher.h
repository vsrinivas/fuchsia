// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_NETEMUL_RUNNER_MANAGED_LAUNCHER_H_
#define GARNET_BIN_NETEMUL_RUNNER_MANAGED_LAUNCHER_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <string>

namespace netemul {
class ManagedEnvironment;
class ManagedLauncher {
 public:
  explicit ManagedLauncher(ManagedEnvironment* environment);

  ~ManagedLauncher();

  void CreateComponent(
      fuchsia::sys::LaunchInfo launch_info,
      fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller);

 private:
  // Pointer to parent environment. Not owned.
  ManagedEnvironment* env_;
  fuchsia::sys::LauncherPtr real_launcher_;
};

}  // namespace netemul

#endif  // GARNET_BIN_NETEMUL_RUNNER_MANAGED_LAUNCHER_H_
