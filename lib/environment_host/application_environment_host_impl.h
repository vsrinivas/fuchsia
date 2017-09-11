// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/svc/cpp/service_namespace.h"
#include "lib/app/fidl/application_environment.fidl.h"
#include "lib/app/fidl/application_environment_host.fidl.h"

namespace maxwell {

// Environment surfacing only explicitly given environment services.
class ApplicationEnvironmentHostImpl : public app::ApplicationEnvironmentHost,
                                       public app::ServiceNamespace {
 public:
  ApplicationEnvironmentHostImpl(app::ApplicationEnvironment* parent_env);
  void GetApplicationEnvironmentServices(
      fidl::InterfaceRequest<app::ServiceProvider> environment_services)
      override;
};

}  // namespace maxwell
