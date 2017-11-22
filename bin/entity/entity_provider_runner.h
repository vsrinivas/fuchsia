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

  // Given a map of entity type -> entity data, creates an entity reference for
  // it. This data is encoded into the entity reference, and must be within
  // 16KB. If successful, a non-null value is returned.
  fidl::String CreateReferenceFromData(
      fidl::Map<fidl::String, fidl::String>* type_to_data);

  // Called by a DataEntity when it has no more |Entity|s it needs to serve for
  // a particular |entity_reference|.
  void OnDataEntityFinished(const std::string& entity_reference);

 private:
  class EntityReferenceFactoryImpl;
  class DataEntity;

  // Called by |EntityReferenceFactoryImpl|.
  void CreateReference(
      const std::string& agent_url,
      const fidl::String& cookie,
      const EntityReferenceFactory::CreateReferenceCallback& callback);

  // |EntityResolver|
  void ResolveEntity(const fidl::String& entity_reference,
                     fidl::InterfaceRequest<Entity> entity_request) override;

  void ResolveDataEntity(const fidl::String& entity_reference,
                         fidl::InterfaceRequest<Entity> entity_request);

  EntityProviderLauncher* const entity_provider_launcher_;

  // component id -> EntityReferenceFactory
  std::map<std::string, std::unique_ptr<EntityReferenceFactoryImpl>>
      entity_reference_factory_bindings_;
  fidl::BindingSet<EntityResolver> entity_resolver_bindings_;

  // These are the running entity providers.
  // component id -> EntityProviderController.
  std::map<std::string, std::unique_ptr<EntityProviderController>>
      entity_provider_controllers_;

  // entity reference -> |Entity| implementation.
  std::map<std::string, std::unique_ptr<DataEntity>> data_entities_;

  FXL_DISALLOW_COPY_AND_ASSIGN(EntityProviderRunner);
};

}  // namespace modular

#endif  // PERIDOT_BIN_ENTITY_ENTITY_PROVIDER_RUNNER_H_
