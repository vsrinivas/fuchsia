// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runner.h"

#include <lib/async/default.h>
#include <lib/fsl/io/fd.h>
#include <lib/fxl/files/unique_fd.h>
#include <lib/fxl/logging.h>
#include <lib/pkg_url/fuchsia_pkg_url.h>
#include "garnet/lib/cmx/cmx.h"

namespace netemul {
using component::StartupContext;
static const char* kSandbox =
    "fuchsia-pkg://fuchsia.com/netemul_sandbox#meta/netemul_sandbox.cmx";

Runner::Runner(async_dispatcher_t* dispatcher) {
  if (dispatcher == nullptr) {
    dispatcher = async_get_default_dispatcher();
  }
  dispatcher_ = dispatcher;

  startup_context_ = StartupContext::CreateFromStartupInfo();
  startup_context_->ConnectToEnvironmentService(
      launcher_.NewRequest(dispatcher_));
  startup_context_->ConnectToEnvironmentService(
      loader_.NewRequest(dispatcher_));
  startup_context_->outgoing().AddPublicService(
      bindings_.GetHandler(this, dispatcher_));
}

struct RunnerArgs {
  fuchsia::sys::StartupInfo startup_info;
  fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller;
};

void Runner::StartComponent(
    fuchsia::sys::Package package, fuchsia::sys::StartupInfo startup_info,
    fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller) {
  FXL_LOG(INFO) << "resolved URL: " << package.resolved_url;

  // args needs to be shared ptr 'cause Loader fidl
  // uses std::function and not fit::function (i.e. legacy callbacks)
  std::shared_ptr<RunnerArgs> args = std::make_shared<RunnerArgs>();
  args->startup_info = std::move(startup_info);
  args->controller = std::move(controller);

  // go through loader to get information, 'cause info is not complete
  // from caller (missing directory and other stuff)
  loader_->LoadUrl(
      package.resolved_url, [this, args](fuchsia::sys::PackagePtr package) {
        RunComponent(std::move(package), std::move(args->startup_info),
                     std::move(args->controller));
      });
}

void Runner::RunComponent(
    fuchsia::sys::PackagePtr package, fuchsia::sys::StartupInfo startup_info,
    fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller) {
  if (!package) {
    // TODO(brunodalbo) expose errors correctly through interface request
    FXL_LOG(ERROR) << "Can't load package";
    return;
  }
  // extract and parse cmx:
  component::FuchsiaPkgUrl fp;
  if (!fp.Parse(package->resolved_url)) {
    FXL_LOG(ERROR) << "can't parse fuchsia URL " << package->resolved_url;
    // TODO(brunodalbo) expose errors correctly through interface request
    return;
  }

  if (!package->directory.is_valid()) {
    FXL_LOG(ERROR) << "Package directory not provided";
    return;
  }

  component::CmxMetadata cmx;
  fxl::UniqueFD fd =
      fsl::OpenChannelAsFileDescriptor(std::move(package->directory));

  json::JSONParser json_parser;
  if (!cmx.ParseFromFileAt(fd.get(), fp.resource_path(), &json_parser)) {
    FXL_LOG(ERROR) << "cmx file failed to parse: " << json_parser.error_str();
    // TODO(brunodalbo) expose errors correctly through interface request
    return;
  }

  std::stringstream launchpkg;
  launchpkg << "fuchsia-pkg://fuchsia.com/" << fp.package_name() << "#"
            << cmx.program_meta().data();

  auto sandbox_arg = launchpkg.str();

  auto linfo = std::move(startup_info.launch_info);
  linfo.url = kSandbox;
  linfo.arguments->push_back(sandbox_arg);
  linfo.arguments->insert(linfo.arguments->end(),
                          startup_info.launch_info.arguments->begin(),
                          startup_info.launch_info.arguments->end());

  launcher_->CreateComponent(std::move(linfo), std::move(controller));
}

}  // namespace netemul
