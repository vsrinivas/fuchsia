// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/entity/entity_provider_controller.h"

#include "peridot/bin/entity/entity_provider_runner.h"

namespace modular {

class EntityProviderController::EntityImpl : Entity {
 public:
  EntityImpl(EntityProviderController* const entity_provider_controller,
             EntityProvider* const entity_provider,
             const std::string& cookie)
      : entity_provider_controller_(entity_provider_controller),
        entity_provider_(entity_provider),
        cookie_(cookie) {
    entity_bindings_.set_on_empty_set_handler([this] {
      entity_provider_controller_->OnEmptyEntityImpls(cookie_);
      // |this| is no longer valid.
    });
  }

  // Serves this |Entity| for the cookie this |EntityImpl| was instantiated
  // for.
  void ProvideEntity(fidl::InterfaceRequest<Entity> request) {
    entity_bindings_.AddBinding(this, std::move(request));
  }

 private:
  // |Entity|
  void GetTypes(const GetTypesCallback& callback) override {
    entity_provider_->GetTypes(cookie_, callback);
  }

  // |Entity|
  void GetData(const fidl::String& type,
               const GetDataCallback& callback) override {
    entity_provider_->GetData(cookie_, type, callback);
  }

  EntityProviderController* const entity_provider_controller_;
  EntityProvider* const entity_provider_;
  const std::string cookie_;
  fidl::BindingSet<Entity> entity_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(EntityImpl);
};

EntityProviderController::EntityProviderController(
    EntityProviderRunner* const entity_provider_runner,
    EntityProviderLauncher* const entity_provider_launcher,
    const std::string& agent_url)
    : entity_provider_runner_(entity_provider_runner), agent_url_(agent_url) {
  FXL_DLOG(INFO) << "Running EntityProvider " << agent_url;
  entity_provider_launcher->ConnectToEntityProvider(
      agent_url_, entity_provider_.NewRequest(),
      agent_controller_.NewRequest());
  agent_controller_.set_connection_error_handler([this] {
    entity_provider_runner_->OnEntityProviderFinished(agent_url_);
    // |this| no longer valid.
  });
}

EntityProviderController::~EntityProviderController() = default;

void EntityProviderController::ProvideEntity(
    const std::string& cookie,
    fidl::InterfaceRequest<Entity> request) {
  auto it = entity_impls_.find(cookie);
  if (it == entity_impls_.end()) {
    bool inserted;
    std::tie(it, inserted) = entity_impls_.insert(std::make_pair(
        cookie,
        std::make_unique<EntityImpl>(this, entity_provider_.get(), cookie)));
    FXL_DCHECK(inserted);
  }
  // When there are no more |Entity|s being serviced for this |cookie|,
  // |OnEmptyEntityImpl()| is triggered.
  it->second->ProvideEntity(std::move(request));
}

void EntityProviderController::OnEmptyEntityImpls(const std::string cookie) {
  entity_impls_.erase(cookie);
  if (entity_impls_.size() == 0ul) {
    // We can drop our connection to the EntityProvider at this point.
    entity_provider_runner_->OnEntityProviderFinished(agent_url_);
    // |this| no longer valid.
  }
}

}  // namespace modular
