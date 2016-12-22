// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

#include "apps/modular/lib/app/application_context.h"
#include "apps/modular/lib/app/service_provider_impl.h"
#include "apps/modular/services/application/application_launcher.fidl.h"
#include "lib/ftl/macros.h"

namespace netconnector {

// Provides services based on service registrations.
class RespondingServiceHost {
 public:
  RespondingServiceHost(const modular::ApplicationEnvironmentPtr& environment);

  ~RespondingServiceHost();

  // Registers a singleton service.
  void RegisterSingleton(const std::string& service_name,
                         modular::ApplicationLaunchInfoPtr launch_info);

  modular::ServiceProvider* services() {
    return static_cast<modular::ServiceProvider*>(&service_provider_);
  }

 private:
  std::unordered_map<std::string, modular::ServiceProviderPtr>
      service_providers_by_name_;
  modular::ServiceProviderImpl service_provider_;
  modular::ApplicationLauncherPtr launcher_;

  FTL_DISALLOW_COPY_AND_ASSIGN(RespondingServiceHost);
};

}  // namespace netconnector
