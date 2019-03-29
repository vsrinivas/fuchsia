// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SESSIONMGR_ENTITY_PROVIDER_RUNNER_ENTITY_PROVIDER_CONTROLLER_H_
#define PERIDOT_BIN_SESSIONMGR_ENTITY_PROVIDER_RUNNER_ENTITY_PROVIDER_CONTROLLER_H_

#include <map>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fidl/cpp/string.h>
#include <src/lib/fxl/macros.h>

namespace modular {

class EntityProviderRunner;

// This class runs and manages the lifetime of an EntityProvider service.
//
// When the entity provider is an agent the controller keeps the agent
// connection alive.
class EntityProviderController {
 public:
  // Creates an controller for a given entity provider.
  //
  // |entity_provider| The provider which is managed by this controller.
  // |agent_controller| If the entity provider is backed by an agent, this is
  //   the associated agent controller. If no such controller exists, nullptr is
  //   acceptable.
  // |done| The callback which is called when the entity provider managed by
  //   this controller has finished running.
  EntityProviderController(
      fuchsia::modular::EntityProviderPtr entity_provider,
      fuchsia::modular::AgentControllerPtr agent_controller,
      fit::function<void()> done);

  ~EntityProviderController();

  // Called by |EntityProviderRunner| when an |fuchsia::modular::Entity| needs
  // to be provided, usually when an entity reference is being resolved to an
  // |fuchsia::modular::Entity|.
  void ProvideEntity(const std::string& cookie,
                     const std::string& entity_reference,
                     fidl::InterfaceRequest<fuchsia::modular::Entity> request);

 private:
  // This class manages the lifetime of all |fuchsia::modular::Entity|s for a
  // given cookie.
  class EntityImpl;

  // Called when there are no more outstanding |fuchsia::modular::Entity|
  // interfaces we need to provide for. At this point, we can tear down the
  // |EntityImpl| providing for this cookie.
  void OnEmptyEntityImpls(const std::string cookie);

  // cookie -> EntityImpl
  std::map<std::string, std::unique_ptr<EntityImpl>> entity_impls_;

  // The managed entity provider connection.
  fuchsia::modular::EntityProviderPtr entity_provider_;

  // The agent controller connection for entity providers which are agents.
  fuchsia::modular::AgentControllerPtr agent_controller_;

  // The callback which is called when the entity provider finishes running.
  fit::function<void()> done_;

  FXL_DISALLOW_COPY_AND_ASSIGN(EntityProviderController);
};

}  // namespace modular

#endif  // PERIDOT_BIN_SESSIONMGR_ENTITY_PROVIDER_RUNNER_ENTITY_PROVIDER_CONTROLLER_H_
