// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "managed_launcher.h"

#include <lib/fdio/io.h>
#include <zircon/status.h>

#include <src/lib/pkg_url/fuchsia_pkg_url.h>

#include "garnet/lib/process/process_builder.h"
#include "managed_environment.h"
#include "src/lib/cmx/cmx.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/fsl/io/fd.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/concatenate.h"

namespace netemul {

using fuchsia::sys::TerminationReason;
using process::ProcessBuilder;

// Helper function to respond with a failure termination reason
// to component controller requests.
void EmitComponentFailure(fidl::InterfaceRequest<fuchsia::sys::ComponentController> req,
                          fuchsia::sys::TerminationReason reason) {
  // Internal helper class to be able to use fidl::Binding to emit the event:
  class ErrorComponentController : public fuchsia::sys::ComponentController {
   public:
    ErrorComponentController(fidl::InterfaceRequest<fuchsia::sys::ComponentController> req,
                             fuchsia::sys::TerminationReason reason)
        : binding_(this, std::move(req)) {
      binding_.events().OnTerminated(-1, reason);
    }

    void Kill() override { /* Do nothing */
    }
    void Detach() override { /* Do nothing */
    }

   private:
    fidl::Binding<fuchsia::sys::ComponentController> binding_;
  };

  ErrorComponentController err(std::move(req), reason);
}

static void CreateFlatNamespace(fuchsia::sys::LaunchInfo* linfo) {
  if (!linfo->flat_namespace) {
    linfo->flat_namespace = std::make_unique<fuchsia::sys::FlatNamespace>();
  }
}

struct LaunchArgs {
  fuchsia::sys::LaunchInfo launch_info;
  fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller;
};

void ManagedLauncher::CreateComponent(
    fuchsia::sys::LaunchInfo launch_info,
    fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller) {
  // because fuchsia.sys.loader uses legacy callbacks, we need to
  // save the info in a shared ptr for the closure
  auto args = std::make_shared<LaunchArgs>();
  args->launch_info = std::move(launch_info);
  args->controller = std::move(controller);

  // load package information
  loader_->LoadUrl(args->launch_info.url, [this, args](fuchsia::sys::PackagePtr package) {
    CreateComponent(std::move(package), std::move(args->launch_info), std::move(args->controller));
  });
}

ManagedLauncher::ManagedLauncher(ManagedEnvironment* environment) : env_(environment) {
  env_->environment().ConnectToService(real_launcher_.NewRequest());
  env_->environment().ConnectToService(loader_.NewRequest());
  env_->environment().ConnectToService(loader_sync_.NewRequest());
}

void ManagedLauncher::Bind(fidl::InterfaceRequest<fuchsia::sys::Launcher> request) {
  bindings_.AddBinding(this, std::move(request));
}

void ManagedLauncher::CreateComponent(
    fuchsia::sys::PackagePtr package, fuchsia::sys::LaunchInfo launch_info,
    fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller) {
  // Before launching, we'll check the component's sandbox
  // so we can inject virtual devices
  if (!package) {
    FXL_LOG(ERROR) << "Can't load package \"" << launch_info.url << "\"";
    EmitComponentFailure(std::move(controller), fuchsia::sys::TerminationReason::PACKAGE_NOT_FOUND);
    return;
  }

  if (!UpdateLaunchInfo(std::move(package), &launch_info)) {
    EmitComponentFailure(std::move(controller), fuchsia::sys::TerminationReason::INTERNAL_ERROR);
    return;
  }

  real_launcher_->CreateComponent(std::move(launch_info), std::move(controller));
}

bool ManagedLauncher::MakeServiceLaunchInfo(fuchsia::sys::LaunchInfo* launch_info) {
  fuchsia::sys::PackagePtr package;
  auto status = loader_sync_->LoadUrl(launch_info->url, &package);
  if (status != ZX_OK || !package) {
    FXL_LOG(ERROR) << "Failed to load service package contents for " << launch_info->url;
    return false;
  }

  return UpdateLaunchInfo(std::move(package), launch_info);
}

bool ManagedLauncher::UpdateLaunchInfo(fuchsia::sys::PackagePtr package,
                                       fuchsia::sys::LaunchInfo* launch_info) {
  if (!package->directory.is_valid()) {
    FXL_LOG(ERROR) << "Package directory not provided";
    return false;
  }

  // let's open and parse the cmx
  component::FuchsiaPkgUrl fp;
  if (!fp.Parse(package->resolved_url)) {
    FXL_LOG(ERROR) << "Can't parse package url " << package->resolved_url;
    return false;
  }

  component::CmxMetadata cmx;
  fbl::unique_fd fd = fsl::OpenChannelAsFileDescriptor(std::move(package->directory));

  json::JSONParser json_parser;
  if (!cmx.ParseFromFileAt(fd.get(), fp.resource_path(), &json_parser)) {
    FXL_LOG(ERROR) << "cmx file failed to parse: " << json_parser.error_str();
    return false;
  }

  // we have devices in sandbox meta, here
  // we just add our own /vdev to the flat namespace
  // this could be improved by filtering /vdev to requested classes only
  // like appmgr does,
  // but seems overkill for testing environments
  if (!cmx.sandbox_meta().dev().empty()) {
    CreateFlatNamespace(launch_info);
    // add all devices to flat namespace:
    launch_info->flat_namespace->paths.emplace_back(kVdevRoot);
    launch_info->flat_namespace->directories.push_back(env_->OpenVdevDirectory());
  }

  if (cmx.sandbox_meta().HasFeature("isolated-persistent-storage")) {
    CreateFlatNamespace(launch_info);
    // add virtual data folder (in-memory fs) to namespace
    launch_info->flat_namespace->paths.emplace_back(kVDataRoot);
    launch_info->flat_namespace->directories.push_back(env_->OpenVdataDirectory());
  }

  if (!launch_info->out) {
    launch_info->out = env_->loggers().CreateLogger(package->resolved_url, false);
  }
  if (!launch_info->err) {
    launch_info->err = env_->loggers().CreateLogger(package->resolved_url, true);
  }

  // increment counter
  env_->loggers().IncrementCounter();
  return true;
}

ManagedLauncher::~ManagedLauncher() = default;

}  // namespace netemul
