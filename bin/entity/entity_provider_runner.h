// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_ENTITY_ENTITY_PROVIDER_RUNNER_H_
#define PERIDOT_BIN_ENTITY_ENTITY_PROVIDER_RUNNER_H_

#include <map>

#include "lib/entity/fidl/entity.fidl.h"
#include "lib/entity/fidl/entity_provider.fidl.h"
#include "lib/entity/fidl/entity_reference_factory.fidl.h"
#include "lib/entity/fidl/entity_resolver.fidl.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/string.h"
#include "lib/fxl/macros.h"

namespace modular {

class EntityProviderController;
class EntityProviderLauncher;

// This class provides an implementation for |EntityResolver| and
// |EntityReferenceFactory| and manages all the EntityProviders running in the
// system. One |EntityProviderRunner| instance services all |EntityResolver|
// interfaces, and there is one |EntityReferenceFactoryImpl| for each
// |EntityReferenceFactory| interface.
class EntityProviderRunner : EntityResolver {
 public:
  EntityProviderRunner(EntityProviderLauncher* entity_provider_launcher);
  ~EntityProviderRunner() override;

  void ConnectEntityReferenceFactory(
      const std::string& agent_url,
      fidl::InterfaceRequest<EntityReferenceFactory> request);
  void ConnectEntityResolver(fidl::InterfaceRequest<EntityResolver> request);

  // Called by an EntityProviderController when the entity provider for a
  // component ID doesn't need to live anymore.
  // TODO(vardhan): Maybe wrap this into an interface used by
  // EntityProviderController.
  void OnEntityProviderFinished(const std::string agent_url);

 private:
  class EntityReferenceFactoryImpl;

  // Called by |EntityReferenceFactoryImpl|.
  void CreateReference(
      const std::string& agent_url,
      const fidl::String& cookie,
      const EntityReferenceFactory::CreateReferenceCallback& callback);

  // |EntityResolver|
  void ResolveEntity(const fidl::String& entity_reference,
                     fidl::InterfaceRequest<Entity> entity_request) override;

  EntityProviderLauncher* const entity_provider_launcher_;

  // component id -> EntityReferenceFactory
  std::map<std::string, std::unique_ptr<EntityReferenceFactoryImpl>>
      entity_reference_factory_bindings_;
  fidl::BindingSet<EntityResolver> entity_resolver_bindings_;

  // These are the running entity providers.
  // component id -> EntityProviderController.
  std::map<std::string, std::unique_ptr<EntityProviderController>>
      entity_provider_controllers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(EntityProviderRunner);
};

}  // namespace modular

#endif  // PERIDOT_BIN_ENTITY_ENTITY_PROVIDER_RUNNER_H_
