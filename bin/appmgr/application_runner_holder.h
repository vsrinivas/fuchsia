// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_APPLICATION_RUNNER_HOLDER_H_
#define GARNET_BIN_APPMGR_APPLICATION_RUNNER_HOLDER_H_

#include <zx/vmo.h>

#include "garnet/bin/appmgr/application_namespace.h"
#include "lib/app/fidl/application_controller.fidl.h"
#include "lib/app/fidl/application_runner.fidl.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"

namespace app {

class ApplicationRunnerHolder {
 public:
  ApplicationRunnerHolder(
      ServiceProviderPtr services,
      ApplicationControllerPtr controller,
      fxl::RefPtr<ApplicationNamespace> application_namespace);
  ~ApplicationRunnerHolder();

  void StartApplication(
      ApplicationPackagePtr package,
      ApplicationStartupInfoPtr startup_info,
      fidl::InterfaceRequest<ApplicationController> controller);

 private:
  ServiceProviderPtr services_;
  ApplicationControllerPtr controller_;
  ApplicationRunnerPtr runner_;

  std::vector<fxl::RefPtr<ApplicationNamespace>> namespaces_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ApplicationRunnerHolder);
};

}  // namespace app

#endif  // GARNET_BIN_APPMGR_APPLICATION_RUNNER_HOLDER_H_
