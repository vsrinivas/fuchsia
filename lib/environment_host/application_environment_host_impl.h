// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/modular/lib/app/service_provider_impl.h"
#include "apps/modular/services/application/application_environment_host.fidl.h"

namespace maxwell {

// Environment surfacing only explicitly given environment services.
class ApplicationEnvironmentHostImpl
    : public modular::ApplicationEnvironmentHost,
      public modular::ServiceProviderImpl {
 public:
  void GetApplicationEnvironmentServices(
      fidl::InterfaceRequest<modular::ServiceProvider> environment_services)
      override;
};

}  // namespace maxwell
