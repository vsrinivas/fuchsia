// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "application/lib/app/service_provider_impl.h"
#include "application/services/application_environment.fidl.h"
#include "application/services/application_environment_host.fidl.h"

namespace maxwell {

// Environment surfacing only explicitly given environment services.
class ApplicationEnvironmentHostImpl
    : public modular::ApplicationEnvironmentHost,
      public modular::ServiceProviderImpl {
 public:
  ApplicationEnvironmentHostImpl(modular::ApplicationEnvironment* parent_env);
  void GetApplicationEnvironmentServices(
      fidl::InterfaceRequest<modular::ServiceProvider> environment_services)
      override;
};

}  // namespace maxwell
