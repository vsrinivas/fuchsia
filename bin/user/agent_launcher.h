// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_USER_AGENT_LAUNCHER_H_
#define PERIDOT_BIN_USER_AGENT_LAUNCHER_H_

#include "lib/app/fidl/application_environment.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "peridot/lib/environment_host/application_environment_host_impl.h"

namespace maxwell {

class AgentLauncher {
 public:
  AgentLauncher(app::ApplicationEnvironment* environment)
      : environment_(environment) {}
  app::ServiceProviderPtr StartAgent(
      const std::string& url,
      std::unique_ptr<app::ApplicationEnvironmentHost> env_host);

 private:
  app::ApplicationEnvironment* environment_;

  fidl::BindingSet<app::ApplicationEnvironmentHost,
                   std::unique_ptr<app::ApplicationEnvironmentHost>>
      agent_host_bindings_;
};

}  // namespace maxwell

#endif  // PERIDOT_BIN_USER_AGENT_LAUNCHER_H_
