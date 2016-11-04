// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_APPLICATION_MANAGER_APPLICATION_RUNNER_HOLDER_H_
#define APPS_MODULAR_APPLICATION_MANAGER_APPLICATION_RUNNER_HOLDER_H_

#include "apps/modular/services/application/application_controller.fidl.h"
#include "apps/modular/services/application/application_runner.fidl.h"
#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/macros.h"

namespace modular {

class ApplicationRunnerHolder {
 public:
  ApplicationRunnerHolder(ServiceProviderPtr services,
                          ApplicationControllerPtr controller);
  ~ApplicationRunnerHolder();

  void StartApplication(
      ftl::UniqueFD fd,
      ApplicationStartupInfoPtr startup_info,
      fidl::InterfaceRequest<ApplicationController> controller);

 private:
  ServiceProviderPtr services_;
  ApplicationControllerPtr controller_;
  ApplicationRunnerPtr runner_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ApplicationRunnerHolder);
};

}  // namespace modular

#endif  // APPS_MODULAR_APPLICATION_MANAGER_APPLICATION_RUNNER_HOLDER_H_
