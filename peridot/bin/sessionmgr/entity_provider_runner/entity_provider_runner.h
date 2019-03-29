// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SESSIONMGR_ENTITY_PROVIDER_RUNNER_ENTITY_PROVIDER_RUNNER_H_
#define PERIDOT_BIN_SESSIONMGR_ENTITY_PROVIDER_RUNNER_ENTITY_PROVIDER_RUNNER_H_

#include <map>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/array.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fidl/cpp/string.h>
#include <src/lib/fxl/macros.h>

namespace modular {

class EntityProviderController;
class EntityProviderLauncher;

// This class provides an implementation for |fuchsia::modular::EntityResolver|
// and |fuchsia::modular::EntityReferenceFactory| and manages all the
// EntityProviders running in the system. One |EntityProviderRunner| instance
// services all |fuchsia::modular::EntityResolver| interfaces, and there is one
// |EntityReferenceFactoryImpl| for each
// |fuchsia::modular::EntityReferenceFactory| interface.
class EntityProviderRunner : public fuchsia::modular::EntityResolver {
 public:
  EntityProviderRunner(EntityProviderLauncher* entity_provider_launcher);
  ~EntityProviderRunner() override;

  // Connects to the entity reference factory for the agent at |agent_url|.
  //
  // The created entity references will be resolved back to that particular
  // agent.
  void ConnectEntityReferenceFactory(
      const std::string& agent_url,
      fidl::InterfaceRequest<fuchsia::modular::EntityReferenceFactory> request);

  // Connects to the entity resolver service. The resolver service can resolve
  // any references, regardless if they are backed by an agent or a story entity
  // provider.
  void ConnectEntityResolver(
      fidl::InterfaceRequest<fuchsia::modular::EntityResolver> request);

  // Creates an entity reference for the given |cookie| associated with the
  // specified |story_id|.
  std::string CreateStoryEntityReference(const std::string& story_id,
                                         const std::string& cookie);

  // Given a map of entity type -> entity data, creates an entity reference for
  // it. This data is encoded into the entity reference, and must be within
  // 16KB. If successful, a non-null value is returned.
  std::string CreateReferenceFromData(
      std::map<std::string, std::string> type_to_data);

  // Called by a DataEntity when it has no more |fuchsia::modular::Entity|s it
  // needs to serve for a particular |entity_reference|.
  void OnDataEntityFinished(const std::string& entity_reference);

 private:
  class EntityReferenceFactoryImpl;
  class DataEntity;

  // The |EntityReferenceFactory| uses this to create entity references for
  // a particular agent.
  //
  // |agent_url| The url of the agent creating the entity reference.
  // |cookie| The cookie identifying the entity.
  // |callback| The callback which is called with the constructed entity
  //   reference.
  void CreateReference(
      const std::string& agent_url, const std::string& cookie,
      fuchsia::modular::EntityReferenceFactory::CreateReferenceCallback
          callback);

  // |fuchsia::modular::EntityResolver|
  void ResolveEntity(
      std::string entity_reference,
      fidl::InterfaceRequest<fuchsia::modular::Entity> entity_request) override;

  void ResolveDataEntity(
      fidl::StringPtr entity_reference,
      fidl::InterfaceRequest<fuchsia::modular::Entity> entity_request);

  // Performs the cleanup required when the entity provider at the provided
  // agent_url finished.
  void OnEntityProviderFinished(const std::string agent_url);

  EntityProviderLauncher* const entity_provider_launcher_;

  // agent url -> EntityReferenceFactoryImpl
  std::map<std::string, std::unique_ptr<EntityReferenceFactoryImpl>>
      entity_reference_factory_bindings_;

  // story id -> StoryEntityReferenceFactoryImpl
  std::map<std::string, std::unique_ptr<EntityReferenceFactoryImpl>>
      story_entity_reference_factory_bindings_;

  fidl::BindingSet<fuchsia::modular::EntityResolver> entity_resolver_bindings_;

  // These are the running entity providers.
  // component id -> EntityProviderController.
  std::map<std::string, std::unique_ptr<EntityProviderController>>
      entity_provider_controllers_;

  // entity reference -> |fuchsia::modular::Entity| implementation.
  std::map<std::string, std::unique_ptr<DataEntity>> data_entities_;

  FXL_DISALLOW_COPY_AND_ASSIGN(EntityProviderRunner);
};

}  // namespace modular

#endif  // PERIDOT_BIN_SESSIONMGR_ENTITY_PROVIDER_RUNNER_ENTITY_PROVIDER_RUNNER_H_
