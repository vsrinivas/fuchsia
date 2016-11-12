// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/modular/lib/app/service_provider_impl.h"
#include "apps/modular/services/application/application_environment_host.fidl.h"

namespace maxwell {

// Leaf environment surfacing only explicitly given environment services. This
// environment does not surface itself, i.e. does not afford agents the ability
// to launch other processes.
//
// TODO(rosswang): rename agents vs acquirers to acquisition agents,
// interpretation agents, and suggestion agents, or acquirers, interpreters, and
// suggesters/proposers? The former sound more formal but the latter are
// shorter.
class AgentEnvironmentHost : public modular::ApplicationEnvironmentHost,
                             public modular::ServiceProviderImpl {
 public:
  void GetApplicationEnvironmentServices(
      fidl::InterfaceRequest<modular::ServiceProvider> environment_services)
      override {
    AddBinding(std::move(environment_services));
  }
};

}  // namespace maxwell
