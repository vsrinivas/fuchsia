// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_USER_RUNNER_ENTITY_PROVIDER_RUNNER_ENTITY_PROVIDER_CONTROLLER_H_
#define PERIDOT_BIN_USER_RUNNER_ENTITY_PROVIDER_RUNNER_ENTITY_PROVIDER_CONTROLLER_H_

#include <map>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fidl/cpp/string.h>
#include <lib/fxl/macros.h>

namespace modular {

class EntityProviderLauncher;
class EntityProviderRunner;

// This class runs and manages the lifetime of an agent's
// fuchsia::modular::EntityProvider service. It holds on to one
// fuchsia::modular::AgentController connection to the agent.
class EntityProviderController {
 public:
  EntityProviderController(EntityProviderRunner* entity_provider_runner,
                           EntityProviderLauncher* entity_provider_launcher,
                           const std::string& agent_url);

  ~EntityProviderController();

  // Called by |EntityProviderRunner| when an |fuchsia::modular::Entity| needs
  // to be provided, usually when an entity reference is being resolved to an
  // |fuchsia::modular::Entity|.
  void ProvideEntity(const std::string& cookie,
                     fidl::InterfaceRequest<fuchsia::modular::Entity> request);

 private:
  // This class manages the lifetime of all |fuchsia::modular::Entity|s for a
  // given cookie.
  class EntityImpl;

  // Called when there are no more outstanding |fuchsia::modular::Entity|
  // interfaces we need to provide for. At this point, we can tear down the
  // |EntityImpl| providing for this cookie.
  void OnEmptyEntityImpls(const std::string cookie);

  EntityProviderRunner* const entity_provider_runner_;
  const std::string agent_url_;
  // cookie -> EntityImpl
  std::map<std::string, std::unique_ptr<EntityImpl>> entity_impls_;
  fuchsia::modular::AgentControllerPtr agent_controller_;
  fuchsia::modular::EntityProviderPtr entity_provider_;

  FXL_DISALLOW_COPY_AND_ASSIGN(EntityProviderController);
};

}  // namespace modular

#endif  // PERIDOT_BIN_USER_RUNNER_ENTITY_PROVIDER_RUNNER_ENTITY_PROVIDER_CONTROLLER_H_
