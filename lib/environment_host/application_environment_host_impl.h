// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_ENVIRONMENT_HOST_APPLICATION_ENVIRONMENT_HOST_IMPL_H_
#define PERIDOT_LIB_ENVIRONMENT_HOST_APPLICATION_ENVIRONMENT_HOST_IMPL_H_

#include "lib/app/fidl/application_environment.fidl.h"
#include "lib/app/fidl/application_environment_host.fidl.h"
#include "lib/svc/cpp/service_namespace.h"

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

#endif  // PERIDOT_LIB_ENVIRONMENT_HOST_APPLICATION_ENVIRONMENT_HOST_IMPL_H_
