// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_USER_AGENT_LAUNCHER_H_
#define PERIDOT_BIN_USER_AGENT_LAUNCHER_H_

#include "lib/app/fidl/application_environment.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/svc/cpp/services.h"
#include "peridot/lib/environment_host/maxwell_service_provider_bridge.h"

namespace maxwell {

class AgentLauncher {
 public:
  AgentLauncher(app::ApplicationEnvironment* environment)
      : environment_(environment) {}
  app::Services StartAgent(
      const std::string& url,
      std::unique_ptr<MaxwellServiceProviderBridge> bridge);

 private:
  app::ApplicationEnvironment* environment_;

  std::unique_ptr<MaxwellServiceProviderBridge> bridge_;
};

}  // namespace maxwell

#endif  // PERIDOT_BIN_USER_AGENT_LAUNCHER_H_
