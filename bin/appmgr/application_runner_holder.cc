// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/application_runner_holder.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <utility>

#include "lib/fsl/vmo/file.h"

namespace app {

ApplicationRunnerHolder::ApplicationRunnerHolder(
    ServiceProviderPtr services,
    ApplicationControllerPtr controller)
    : services_(std::move(services)), controller_(std::move(controller)) {
  services_->ConnectToService(ApplicationRunner::Name_,
                              runner_.NewRequest().PassChannel());
}

ApplicationRunnerHolder::~ApplicationRunnerHolder() = default;

void ApplicationRunnerHolder::StartApplication(
    ApplicationPackagePtr package,
    ApplicationStartupInfoPtr startup_info,
    fidl::InterfaceRequest<ApplicationController> controller) {
  runner_->StartApplication(std::move(package), std::move(startup_info),
                            std::move(controller));
}

}  // namespace app
