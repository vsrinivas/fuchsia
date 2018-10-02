// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_MAXWELL_AGENT_LAUNCHER_H_
#define PERIDOT_BIN_MAXWELL_AGENT_LAUNCHER_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/svc/cpp/services.h>

#include "peridot/lib/environment_host/maxwell_service_provider_bridge.h"

namespace maxwell {

class AgentLauncher {
 public:
  AgentLauncher(fuchsia::sys::Environment* environment)
      : environment_(environment) {}
  component::Services StartAgent(
      const std::string& url,
      std::unique_ptr<MaxwellServiceProviderBridge> bridge);

 private:
  fuchsia::sys::Environment* environment_;

  std::unique_ptr<MaxwellServiceProviderBridge> bridge_;
};

}  // namespace maxwell

#endif  // PERIDOT_BIN_MAXWELL_AGENT_LAUNCHER_H_
