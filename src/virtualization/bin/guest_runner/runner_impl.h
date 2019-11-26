// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_GUEST_RUNNER_RUNNER_IMPL_H_
#define SRC_VIRTUALIZATION_BIN_GUEST_RUNNER_RUNNER_IMPL_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include <fs/synchronous_vfs.h>

namespace guest_runner {

class RunnerImpl : public fuchsia::sys::Runner {
 public:
  RunnerImpl();

  RunnerImpl(const RunnerImpl&) = delete;
  RunnerImpl& operator=(const RunnerImpl&) = delete;

 private:
  // |fuchsia::sys::Runner|
  void StartComponent(
      fuchsia::sys::Package application, fuchsia::sys::StartupInfo startup_info,
      ::fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller) override;

  std::unique_ptr<sys::ComponentContext> context_;
  fuchsia::sys::LauncherPtr launcher_;
  fidl::BindingSet<fuchsia::sys::Runner> bindings_;
  fs::SynchronousVfs vfs_;
};

}  // namespace guest_runner

#endif  // SRC_VIRTUALIZATION_BIN_GUEST_RUNNER_RUNNER_IMPL_H_
