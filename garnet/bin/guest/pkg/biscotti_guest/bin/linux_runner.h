// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_PKG_BISCOTTI_GUEST_BIN_LINUX_RUNNER_H_
#define GARNET_BIN_GUEST_PKG_BISCOTTI_GUEST_BIN_LINUX_RUNNER_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fxl/command_line.h>

#include "garnet/bin/guest/pkg/biscotti_guest/bin/guest.h"

namespace biscotti {

class LinuxRunner : public fuchsia::sys::Runner {
 public:
  LinuxRunner();

  zx_status_t Init(fxl::CommandLine cl);

  LinuxRunner(const LinuxRunner&) = delete;
  LinuxRunner& operator=(const LinuxRunner&) = delete;

 private:
  // |fuchsia::sys::Runner|
  void StartComponent(
      fuchsia::sys::Package application, fuchsia::sys::StartupInfo startup_info,
      ::fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller)
      override;

  std::unique_ptr<component::StartupContext> context_;
  fidl::BindingSet<fuchsia::sys::Runner> bindings_;
  std::unique_ptr<biscotti::Guest> guest_;
};

}  // namespace biscotti

#endif  // GARNET_BIN_GUEST_PKG_BISCOTTI_GUEST_BIN_LINUX_RUNNER_H_
