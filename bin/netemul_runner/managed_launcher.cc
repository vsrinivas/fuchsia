// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "managed_launcher.h"
#include <lib/fdio/io.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/strings/concatenate.h>
#include <zircon/status.h>
#include "garnet/lib/process/process_builder.h"
#include "managed_environment.h"

namespace netemul {

using fuchsia::sys::TerminationReason;
using process::ProcessBuilder;

struct LaunchArgs {
  std::string binary;
  fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller;
};

void ManagedLauncher::CreateComponent(
    fuchsia::sys::LaunchInfo launch_info,
    fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller) {
  // Just pass through to real launcher.
  real_launcher_->CreateComponent(std::move(launch_info),
                                  std::move(controller));
}

ManagedLauncher::ManagedLauncher(ManagedEnvironment* environment)
    : env_(environment) {
  env_->environment().ConnectToService(real_launcher_.NewRequest());
}

ManagedLauncher::~ManagedLauncher() = default;

}  // namespace netemul
