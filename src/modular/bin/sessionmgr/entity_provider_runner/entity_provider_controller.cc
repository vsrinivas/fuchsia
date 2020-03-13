// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/entity_provider_runner/entity_provider_controller.h"

#include "src/lib/syslog/cpp/logger.h"
#include "src/modular/bin/sessionmgr/entity_provider_runner/entity_provider_runner.h"

namespace modular {

class EntityProviderController::EntityImpl : fuchsia::modular::Entity {
 public:
  EntityImpl(EntityProviderController* const entity_provider_controller,
             fuchsia::modular::EntityProvider* const entity_provider, const std::string& cookie,
             const std::string& entity_reference)
      : entity_provider_controller_(entity_provider_controller),
        entity_provider_(entity_provider),
        cookie_(cookie),
        entity_reference_(entity_reference) {
    entity_bindings_.set_empty_set_handler([this] {
      entity_provider_controller_->OnEmptyEntityImpls(cookie_);
      // |this| is no longer valid.
    });
  }

  // Serves this |fuchsia::modular::Entity| for the cookie this |EntityImpl| was
  // instantiated for.
  void ProvideEntity(fidl::InterfaceRequest<fuchsia::modular::Entity> request) {
    entity_bindings_.AddBinding(this, std::move(request));
  }

 private:
  // |fuchsia::modular::Entity|
  void GetTypes(GetTypesCallback callback) override {
    entity_provider_->GetTypes(cookie_, std::move(callback));
  }

  // |fuchsia::modular::Entity|
  void GetData(std::string type, GetDataCallback callback) override {
    entity_provider_->GetData(cookie_, type, std::move(callback));
  }

  // |fuchsia::modular::Entity|
  void WriteData(std::string type, fuchsia::mem::Buffer data, WriteDataCallback callback) override {
    entity_provider_->WriteData(cookie_, type, std::move(data), std::move(callback));
  }

  // |fuchsia::modular::Entity|
  void GetReference(GetReferenceCallback callback) override { callback(entity_reference_); }

  // |fuchsia::modular::Entity|
  void Watch(std::string type,
             fidl::InterfaceHandle<fuchsia::modular::EntityWatcher> watcher) override {
    entity_provider_->Watch(cookie_, type, std::move(watcher));
  }

  EntityProviderController* const entity_provider_controller_;
  fuchsia::modular::EntityProvider* const entity_provider_;
  const std::string cookie_;
  const std::string entity_reference_;
  fidl::BindingSet<fuchsia::modular::Entity> entity_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(EntityImpl);
};

EntityProviderController::EntityProviderController(
    fuchsia::modular::EntityProviderPtr entity_provider,
    fuchsia::modular::AgentControllerPtr agent_controller, fit::function<void()> done)
    : entity_provider_(std::move(entity_provider)),
      agent_controller_(std::move(agent_controller)),
      done_(std::move(done)) {
  FX_DLOGS(INFO) << "Running fuchsia::modular::EntityProvider";
  if (agent_controller_) {
    agent_controller_.set_error_handler([this](zx_status_t status) {
      done_();
      // |this| no longer valid.
    });
  }
}

EntityProviderController::~EntityProviderController() = default;

void EntityProviderController::ProvideEntity(
    const std::string& cookie, const std::string& entity_reference,
    fidl::InterfaceRequest<fuchsia::modular::Entity> request) {
  auto it = entity_impls_.find(cookie);
  if (it == entity_impls_.end()) {
    bool inserted;
    std::tie(it, inserted) = entity_impls_.insert(std::make_pair(
        cookie,
        std::make_unique<EntityImpl>(this, entity_provider_.get(), cookie, entity_reference)));
    FX_DCHECK(inserted);
  }
  // When there are no more |fuchsia::modular::Entity|s being serviced for this
  // |cookie|, |OnEmptyEntityImpl()| is triggered.
  it->second->ProvideEntity(std::move(request));
}

void EntityProviderController::OnEmptyEntityImpls(const std::string cookie) {
  entity_impls_.erase(cookie);
  if (entity_impls_.size() == 0ul) {
    // We can drop our connection to the fuchsia::modular::EntityProvider at
    // this point.
    done_();
    // |this| no longer valid.
  }
}

}  // namespace modular
