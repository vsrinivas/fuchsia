// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runner.h"

#include <lib/async/default.h>

#include <src/lib/pkg_url/fuchsia_pkg_url.h>

#include "src/lib/cmx/cmx.h"
#include "src/lib/fsl/io/fd.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/concatenate.h"

namespace netemul {
static const char* kSandbox = "fuchsia-pkg://fuchsia.com/netemul_sandbox#meta/netemul_sandbox.cmx";
static const char* kDefinitionArg = "--definition=";
static const char* kDefinitionRoot = "/definition";

Runner::Runner(async_dispatcher_t* dispatcher) {
  if (dispatcher == nullptr) {
    dispatcher = async_get_default_dispatcher();
  }
  dispatcher_ = dispatcher;

  component_context_ = sys::ComponentContext::Create();
  component_context_->svc()->Connect(launcher_.NewRequest(dispatcher_));
  component_context_->svc()->Connect(loader_.NewRequest(dispatcher_));
  component_context_->outgoing()->AddPublicService(bindings_.GetHandler(this, dispatcher_));
}

struct RunnerArgs {
  fuchsia::sys::StartupInfo startup_info;
  fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller;
};

void Runner::StartComponent(fuchsia::sys::Package package, fuchsia::sys::StartupInfo startup_info,
                            fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller) {
  FXL_LOG(INFO) << "resolved URL: " << package.resolved_url;

  // args needs to be shared ptr 'cause Loader fidl
  // uses std::function and not fit::function (i.e. legacy callbacks)
  std::shared_ptr<RunnerArgs> args = std::make_shared<RunnerArgs>();
  args->startup_info = std::move(startup_info);
  args->controller = std::move(controller);

  // go through loader to get information, 'cause info is not complete
  // from caller (missing directory and other stuff)
  loader_->LoadUrl(package.resolved_url, [this, args](fuchsia::sys::PackagePtr package) {
    RunComponent(std::move(package), std::move(args->startup_info), std::move(args->controller));
  });
}

void Runner::RunComponent(fuchsia::sys::PackagePtr package, fuchsia::sys::StartupInfo startup_info,
                          fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller) {
  if (!package) {
    FXL_LOG(ERROR) << "Can't load package";
    return;
  }
  // extract and parse cmx:
  component::FuchsiaPkgUrl fp;
  if (!fp.Parse(package->resolved_url)) {
    FXL_LOG(ERROR) << "can't parse fuchsia URL " << package->resolved_url;
    return;
  }

  if (!package->directory.is_valid()) {
    FXL_LOG(ERROR) << "Package directory not provided";
    return;
  }

  auto linfo = std::move(startup_info.launch_info);
  auto incoming_args = std::move(linfo.arguments);
  linfo.url = kSandbox;
  linfo.arguments->clear();
  linfo.arguments->push_back(fxl::Concatenate(
      {std::string(kDefinitionArg), std::string(kDefinitionRoot), "/", fp.resource_path()}));

  if (!linfo.flat_namespace) {
    linfo.flat_namespace = fuchsia::sys::FlatNamespace::New();
  }
  linfo.flat_namespace->paths.emplace_back(kDefinitionRoot);
  linfo.flat_namespace->directories.push_back(std::move(package->directory));

  if (!incoming_args->empty()) {
    linfo.arguments->push_back("--");
    linfo.arguments->insert(linfo.arguments->end(), incoming_args->begin(), incoming_args->end());
  }

  launcher_->CreateComponent(std::move(linfo), std::move(controller));
}

}  // namespace netemul
