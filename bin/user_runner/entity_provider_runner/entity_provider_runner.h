// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_USER_RUNNER_ENTITY_PROVIDER_RUNNER_ENTITY_PROVIDER_RUNNER_H_
#define PERIDOT_BIN_USER_RUNNER_ENTITY_PROVIDER_RUNNER_ENTITY_PROVIDER_RUNNER_H_

#include <map>

#include <fuchsia/modular/cpp/fidl.h>

#include "lib/fidl/cpp/array.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/fidl/cpp/string.h"
#include "lib/fxl/macros.h"

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

  void ConnectEntityReferenceFactory(
      const std::string& agent_url,
      fidl::InterfaceRequest<fuchsia::modular::EntityReferenceFactory> request);
  void ConnectEntityResolver(
      fidl::InterfaceRequest<fuchsia::modular::EntityResolver> request);

  // Called by an EntityProviderController when the entity provider for a
  // component ID doesn't need to live anymore.
  // TODO(vardhan): Maybe wrap this into an interface used by
  // EntityProviderController.
  void OnEntityProviderFinished(const std::string agent_url);

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

  // Called by |EntityReferenceFactoryImpl|.
  void CreateReference(
      const std::string& agent_url, fidl::StringPtr cookie,
      fuchsia::modular::EntityReferenceFactory::CreateReferenceCallback
          callback);

  // |fuchsia::modular::EntityResolver|
  void ResolveEntity(
      fidl::StringPtr entity_reference,
      fidl::InterfaceRequest<fuchsia::modular::Entity> entity_request) override;

  void ResolveDataEntity(
      fidl::StringPtr entity_reference,
      fidl::InterfaceRequest<fuchsia::modular::Entity> entity_request);

  EntityProviderLauncher* const entity_provider_launcher_;

  // component id -> fuchsia::modular::EntityReferenceFactory
  std::map<std::string, std::unique_ptr<EntityReferenceFactoryImpl>>
      entity_reference_factory_bindings_;
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

#endif  // PERIDOT_BIN_USER_RUNNER_ENTITY_PROVIDER_RUNNER_ENTITY_PROVIDER_RUNNER_H_
