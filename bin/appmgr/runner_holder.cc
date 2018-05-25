// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/runner_holder.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <utility>

#include "lib/fsl/vmo/file.h"

namespace component {

RunnerHolder::RunnerHolder(Services services, ComponentControllerPtr controller)
    : services_(std::move(services)), controller_(std::move(controller)) {
  services_.ConnectToService(runner_.NewRequest());
}

RunnerHolder::~RunnerHolder() = default;

void RunnerHolder::StartComponent(
    Package package, StartupInfo startup_info,
    std::unique_ptr<archive::FileSystem> file_system, fxl::RefPtr<Namespace> ns,
    fidl::InterfaceRequest<ComponentController> controller) {
  file_systems_.push_back(std::move(file_system));
  namespaces_.push_back(std::move(ns));
  runner_->StartComponent(std::move(package), std::move(startup_info),
                          std::move(controller));
}

}  // namespace component
