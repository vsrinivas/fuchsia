// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/src/application_environment_host_impl.h"
#include "apps/modular/services/application/application_environment.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"

namespace maxwell {

class AgentLauncher {
 public:
  AgentLauncher(modular::ApplicationEnvironment* environment)
      : environment_(environment) {}
  void StartAgent(
      const std::string& url,
      std::unique_ptr<modular::ApplicationEnvironmentHost> env_host);

 private:
  modular::ApplicationEnvironment* environment_;

  fidl::BindingSet<modular::ApplicationEnvironmentHost,
                   std::unique_ptr<modular::ApplicationEnvironmentHost>>
      agent_host_bindings_;
};

}  // namespace maxwell
