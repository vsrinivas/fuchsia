// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_USER_RUNNER_ENTITY_PROVIDER_RUNNER_ENTITY_PROVIDER_LAUNCHER_H_
#define PERIDOT_BIN_USER_RUNNER_ENTITY_PROVIDER_RUNNER_ENTITY_PROVIDER_LAUNCHER_H_

#include <string>

#include <fuchsia/modular/cpp/fidl.h>

#include "lib/fidl/cpp/interface_request.h"
#include "lib/fxl/macros.h"

namespace modular {

// An interface for launching EntityProviders. This interface helps break the
// dependency cycle between |AgentRunner| and |EntityProviderRunner|.
class EntityProviderLauncher {
 public:
  virtual void ConnectToEntityProvider(
      const std::string& agent_url,
      fidl::InterfaceRequest<fuchsia::modular::EntityProvider>
          entity_provider_request,
      fidl::InterfaceRequest<fuchsia::modular::AgentController>
          agent_controller_request) = 0;

 protected:
  virtual ~EntityProviderLauncher();
};

}  // namespace modular

#endif  // PERIDOT_BIN_USER_RUNNER_ENTITY_PROVIDER_RUNNER_ENTITY_PROVIDER_LAUNCHER_H_
