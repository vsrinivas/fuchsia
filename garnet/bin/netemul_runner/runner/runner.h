// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_NETEMUL_RUNNER_RUNNER_RUNNER_H_
#define GARNET_BIN_NETEMUL_RUNNER_RUNNER_RUNNER_H_

#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/binding_set.h>
#include "fuchsia/sys/cpp/fidl.h"

namespace netemul {
class Runner : public fuchsia::sys::Runner {
 public:
  using FRunner = fuchsia::sys::Runner;
  explicit Runner(async_dispatcher_t* dispatcher = nullptr);

  void StartComponent(fuchsia::sys::Package package,
                      fuchsia::sys::StartupInfo startup_info,
                      fidl::InterfaceRequest<fuchsia::sys::ComponentController>
                          controller) override;

 private:
  void RunComponent(
      fuchsia::sys::PackagePtr package, fuchsia::sys::StartupInfo startup_info,
      fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller);

  async_dispatcher_t* dispatcher_;
  std::unique_ptr<component::StartupContext> startup_context_;
  fidl::BindingSet<FRunner> bindings_;
  fuchsia::sys::LauncherPtr launcher_;
  fuchsia::sys::LoaderPtr loader_;
};

}  // namespace netemul

#endif  // GARNET_BIN_NETEMUL_RUNNER_RUNNER_RUNNER_H_
