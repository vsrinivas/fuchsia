// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_PACKAGES_BISCOTTI_GUEST_LINUX_RUNNER_LINUX_RUNNER_H_
#define SRC_VIRTUALIZATION_PACKAGES_BISCOTTI_GUEST_LINUX_RUNNER_LINUX_RUNNER_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include "src/virtualization/packages/biscotti_guest/linux_runner/guest.h"

namespace linux_runner {

class LinuxRunner : public fuchsia::sys::Runner {
 public:
  LinuxRunner();

  zx_status_t Init();

  LinuxRunner(const LinuxRunner&) = delete;
  LinuxRunner& operator=(const LinuxRunner&) = delete;

 private:
  // |fuchsia::sys::Runner|
  void StartComponent(
      fuchsia::sys::Package application, fuchsia::sys::StartupInfo startup_info,
      ::fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller)
      override;

  std::unique_ptr<sys::ComponentContext> context_;
  fidl::BindingSet<fuchsia::sys::Runner> bindings_;
  std::unique_ptr<linux_runner::Guest> guest_;
};

}  // namespace linux_runner

#endif  // SRC_VIRTUALIZATION_PACKAGES_BISCOTTI_GUEST_LINUX_RUNNER_LINUX_RUNNER_H_
