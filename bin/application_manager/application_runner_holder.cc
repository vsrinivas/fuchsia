// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/application_manager/application_runner_holder.h"

#include <mx/vmo.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <utility>

#include "lib/mtl/vmo/file.h"

namespace modular {

ApplicationRunnerHolder::ApplicationRunnerHolder(
    ServiceProviderPtr services,
    ApplicationControllerPtr controller)
    : services_(std::move(services)), controller_(std::move(controller)) {
  services_->ConnectToService(ApplicationRunner::Name_,
                              fidl::GetProxy(&runner_).PassChannel());
}

ApplicationRunnerHolder::~ApplicationRunnerHolder() = default;

void ApplicationRunnerHolder::StartApplication(
    ftl::UniqueFD fd,
    ApplicationStartupInfoPtr startup_info,
    fidl::InterfaceRequest<ApplicationController> controller) {
  mx::vmo data;
  // TODO(abarth): This copy should be asynchronous.
  if (!mtl::VmoFromFd(std::move(fd), &data)) {
    FTL_LOG(ERROR) << "Cannot run " << startup_info->launch_info->url
                   << " because URL is unreadable.";
    return;
  }

  ApplicationPackagePtr package = ApplicationPackage::New();
  package->data = std::move(data);
  runner_->StartApplication(std::move(package), std::move(startup_info),
                            std::move(controller));
}

}  // namespace modular
