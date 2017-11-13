// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/entity/entity_provider_runner.h"

#include <utility>

#include "lib/fxl/macros.h"
#include "lib/fxl/strings/join_strings.h"
#include "peridot/bin/entity/entity_provider_controller.h"
#include "peridot/lib/util/string_escape.h"

namespace modular {
namespace {

constexpr char kEntityReferencePrefix[] = "EntityRef";

// TODO(vardhan): Make entity references secure (no introspection allowed).
// Given an agent_url and a cookie, encodes it into an entity reference.
std::string EncodeEntityReference(const std::string& agent_url,
                                  const fidl::String& cookie) {
  std::vector<std::string> parts(3);
  parts[0] = kEntityReferencePrefix;
  parts[1] = StringEscape(agent_url, "/");
  auto cookie_str = cookie.To<std::string>();
  parts[2] = StringEscape(cookie_str, "/");
  return fxl::JoinStrings(parts, "/");
}

// Inverse of EncodeEntityReference.
bool DecodeEntityReference(const std::string& entity_reference,
                           std::string* const agent_url,
                           std::string* const cookie) {
  auto parts = SplitEscapedString(entity_reference, '/');
  if (parts.size() != 3 || StringUnescape(parts[0]) != kEntityReferencePrefix) {
    return false;
  }
  *agent_url = StringUnescape(parts[1].ToString());
  *cookie = StringUnescape(parts[2].ToString());
  return true;
}

}  // namespace

class EntityProviderRunner::EntityReferenceFactoryImpl
    : EntityReferenceFactory {
 public:
  EntityReferenceFactoryImpl(const std::string& agent_url,
                             EntityProviderRunner* const entity_provider_runner)
      : agent_url_(agent_url),
        entity_provider_runner_(entity_provider_runner) {}

  void AddBinding(fidl::InterfaceRequest<EntityReferenceFactory> request) {
    bindings_.AddBinding(this, std::move(request));
  }

  void set_on_empty_set_handler(const std::function<void()>& handler) {
    bindings_.set_on_empty_set_handler(handler);
  }

 private:
  // |EntityReferenceFactory|
  void CreateReference(const fidl::String& cookie,
                       const CreateReferenceCallback& callback) override {
    entity_provider_runner_->CreateReference(agent_url_, cookie, callback);
  }

  const std::string agent_url_;
  EntityProviderRunner* const entity_provider_runner_;
  fidl::BindingSet<EntityReferenceFactory> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(EntityReferenceFactoryImpl);
};

EntityProviderRunner::EntityProviderRunner(
    EntityProviderLauncher* const entity_provider_launcher)
    : entity_provider_launcher_(entity_provider_launcher) {}

EntityProviderRunner::~EntityProviderRunner() = default;

void EntityProviderRunner::ConnectEntityReferenceFactory(
    const std::string& agent_url,
    fidl::InterfaceRequest<EntityReferenceFactory> request) {
  auto it = entity_reference_factory_bindings_.find(agent_url);
  if (it == entity_reference_factory_bindings_.end()) {
    bool inserted;
    std::tie(it, inserted) = entity_reference_factory_bindings_.emplace(
        std::make_pair(agent_url, std::make_unique<EntityReferenceFactoryImpl>(
                                      agent_url, this)));
    FXL_DCHECK(inserted);
    it->second->set_on_empty_set_handler([this, agent_url] {
      entity_reference_factory_bindings_.erase(agent_url);
    });
  }
  it->second->AddBinding(std::move(request));
}

void EntityProviderRunner::ConnectEntityResolver(
    fidl::InterfaceRequest<EntityResolver> request) {
  entity_resolver_bindings_.AddBinding(this, std::move(request));
}

void EntityProviderRunner::OnEntityProviderFinished(
    const std::string agent_url) {
  entity_provider_controllers_.erase(agent_url);
}

void EntityProviderRunner::CreateReference(
    const std::string& agent_url,
    const fidl::String& cookie,
    const EntityReferenceFactory::CreateReferenceCallback& callback) {
  auto entity_ref = EncodeEntityReference(agent_url, cookie);
  callback(entity_ref);
}

void EntityProviderRunner::ResolveEntity(
    const fidl::String& entity_reference,
    fidl::InterfaceRequest<Entity> entity_request) {
  std::string agent_url;
  std::string cookie;
  if (!DecodeEntityReference(entity_reference, &agent_url, &cookie)) {
    FXL_DLOG(ERROR) << "Could not resolve entity reference: "
                    << entity_reference;
    return;
    // |entity_request| is closed here.
  }

  // Connect to the EntityProviderController managing this entity.
  auto it = entity_provider_controllers_.find(agent_url);
  if (it == entity_provider_controllers_.end()) {
    bool inserted;
    std::tie(it, inserted) = entity_provider_controllers_.insert(std::make_pair(
        agent_url, std::make_unique<EntityProviderController>(
                       this, entity_provider_launcher_, agent_url)));
    FXL_DCHECK(inserted);
  }

  it->second->ProvideEntity(cookie, std::move(entity_request));
}

}  // namespace modular
