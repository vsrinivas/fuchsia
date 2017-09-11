// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/app/fidl/application_environment.fidl.h"
#include "apps/maxwell/src/application_environment_host_impl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"

namespace maxwell {

class AgentLauncher {
 public:
  AgentLauncher(app::ApplicationEnvironment* environment)
      : environment_(environment) {}
  void StartAgent(const std::string& url,
                  std::unique_ptr<app::ApplicationEnvironmentHost> env_host);

 private:
  app::ApplicationEnvironment* environment_;

  fidl::BindingSet<app::ApplicationEnvironmentHost,
                   std::unique_ptr<app::ApplicationEnvironmentHost>>
      agent_host_bindings_;
};

}  // namespace maxwell
