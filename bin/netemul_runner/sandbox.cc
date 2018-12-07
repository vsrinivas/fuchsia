// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sandbox.h"
#include <fcntl.h>
#include <lib/async/default.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/component/cpp/testing/test_util.h>
#include <lib/fdio/watcher.h>
#include <lib/fsl/io/fd.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/strings/concatenate.h>
#include <lib/pkg_url/fuchsia_pkg_url.h>
#include <zircon/status.h>
#include "garnet/lib/cmx/cmx.h"

namespace netemul {

Sandbox::Sandbox(SandboxArgs args) : args_(std::move(args)) {
  auto startup_context = component::StartupContext::CreateFromStartupInfo();
  startup_context->ConnectToEnvironmentService(parent_env_.NewRequest());
  startup_context->ConnectToEnvironmentService(loader_.NewRequest());
}

void Sandbox::Start() {
  if (!parent_env_ || !loader_) {
    Terminate(TerminationReason::INTERNAL_ERROR);
  }

  loader_->LoadUrl(args_.package, [this](fuchsia::sys::PackagePtr package) {
    if (!package) {
      Terminate(TerminationReason::PACKAGE_NOT_FOUND);
      return;
    } else if (!package->directory) {
      Terminate(TerminationReason::INTERNAL_ERROR);
    }
    LoadPackage(std::move(package));
  });
}

void Sandbox::Terminate(int64_t exit_code, Sandbox::TerminationReason reason) {
  if (termination_callback_) {
    termination_callback_(exit_code, reason);
  }
}

void Sandbox::Terminate(Sandbox::TerminationReason reason) {
  Terminate(-1, reason);
}
void Sandbox::LoadPackage(fuchsia::sys::PackagePtr package) {
  // package is loaded, proceed to parsing cmx and starting child env
  component::FuchsiaPkgUrl pkgUrl;
  if (!pkgUrl.Parse(package->resolved_url)) {
    FXL_LOG(ERROR) << "Can't parse fuchsia url: " << package->resolved_url;
    Terminate(TerminationReason::INTERNAL_ERROR);
    return;
  }

  fxl::UniqueFD dirfd =
      fsl::OpenChannelAsFileDescriptor(std::move(package->directory));
  sandbox_env_ = std::make_shared<SandboxEnv>(args_.package, std::move(dirfd));

  component::CmxMetadata cmx;

  json::JSONParser json_parser;
  if (!cmx.ParseFromFileAt(sandbox_env_->dir().get(), pkgUrl.resource_path(),
                           &json_parser)) {
    FXL_LOG(ERROR) << "cmx file failed to parse: " << json_parser.error_str();
    Terminate(TerminationReason::INTERNAL_ERROR);
    return;
  }

  root_ = ManagedEnvironment::CreateRoot(parent_env_, sandbox_env_);

  // TODO(brunodalbo) parameterize environment based on
  // facets in cmx file

  root_->SetRunningCallback([this] {
    root_proc_.events().OnTerminated = [this](int64_t code,
                                              TerminationReason reason) {
      Terminate(code, reason);  // mimic termination of root process
    };

    // start root test process:
    fuchsia::sys::LaunchInfo linfo;
    linfo.url = sandbox_env_->name();
    linfo.out = component::testing::CloneFileDescriptor(STDOUT_FILENO);
    linfo.err = component::testing::CloneFileDescriptor(STDERR_FILENO);
    root_->launcher().CreateComponent(std::move(linfo),
                                      root_proc_.NewRequest());
  });
}

}  // namespace netemul