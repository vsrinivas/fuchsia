// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(vardhan): Make entity references secure (no introspection allowed).

#include "src/modular/bin/sessionmgr/entity_provider_runner/entity_provider_runner.h"

#include <utility>

#include "src/lib/fsl/types/type_converters.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/modular/bin/sessionmgr/entity_provider_runner/entity_provider_controller.h"
#include "src/modular/bin/sessionmgr/entity_provider_runner/entity_provider_launcher.h"
#include "src/modular/lib/fidl/json_xdr.h"
#include "src/modular/lib/string_escape/string_escape.h"

namespace modular {
namespace {

constexpr char kEntityReferencePrefix[] = "EntityRef";
constexpr char kEntityDataReferencePrefix[] = "EntityData";
constexpr char kStoryEntityReferencePrefix[] = "Story";

using StringMap = std::map<std::string, std::string>;
constexpr XdrFilterType<StringMap> XdrStringMap[] = {
    XdrFilter<StringMap>,
    nullptr,
};

// Given a |entity_namespace|, |provider_uri| and a |cookie|, encodes it into an
// entity reference.
std::string EncodeEntityReference(const std::string& entity_namespace,
                                  const std::string& provider_uri, const std::string& cookie) {
  std::vector<std::string> parts(3);
  parts[0] = entity_namespace;
  parts[1] = StringEscape(provider_uri, "/");
  parts[2] = StringEscape(cookie, "/");
  return fxl::JoinStrings(parts, "/");
}

// Returns an entity reference for an entity associated with the given
// |story_id| and |cookie|.
std::string EncodeStoryEntityReference(const std::string& story_id, const std::string& cookie) {
  return EncodeEntityReference(kStoryEntityReferencePrefix, story_id, cookie);
}

// Returns an entity reference for an entity associated with the given
// |agent_url| and |cookie|.
std::string EncodeAgentEntityReference(const std::string& agent_url, const std::string& cookie) {
  return EncodeEntityReference(kEntityReferencePrefix, agent_url, cookie);
}

// Inverse of EncodeEntityReference.
bool DecodeEntityReference(const std::string& entity_reference, std::string* const prefix,
                           std::string* const provider_uri, std::string* const cookie) {
  auto parts = SplitEscapedString(entity_reference, '/');
  if (parts.size() != 3) {
    return false;
  }
  *prefix = StringUnescape(parts[0].ToString());
  *provider_uri = StringUnescape(parts[1].ToString());
  *cookie = StringUnescape(parts[2].ToString());
  return true;
}

bool DecodeEntityDataReference(const std::string& entity_reference,
                               std::map<std::string, std::string>* data) {
  auto parts = SplitEscapedString(entity_reference, '/');
  if (parts.size() != 2 || StringUnescape(parts[0]) != kEntityDataReferencePrefix) {
    return false;
  }

  return XdrRead(StringUnescape(parts[1].ToString()), data, XdrStringMap);
}

}  // namespace

class EntityProviderRunner::EntityReferenceFactoryImpl : fuchsia::modular::EntityReferenceFactory {
 public:
  // Creates an entity reference factory for the given |provider_uri|.
  //
  // |entity_provider_runner| exposes the concrete implementations for encoding
  //  entity references.
  EntityReferenceFactoryImpl(const std::string& agent_url,
                             EntityProviderRunner* const entity_provider_runner)
      : agent_url_(agent_url), entity_provider_runner_(entity_provider_runner) {}

  void AddBinding(fidl::InterfaceRequest<fuchsia::modular::EntityReferenceFactory> request) {
    bindings_.AddBinding(this, std::move(request));
  }

  void set_empty_set_handler(fit::function<void()> handler) {
    bindings_.set_empty_set_handler(std::move(handler));
  }

 private:
  // |fuchsia::modular::EntityReferenceFactory|
  void CreateReference(std::string cookie, CreateReferenceCallback callback) override {
    entity_provider_runner_->CreateReference(agent_url_, cookie, std::move(callback));
  }

  // The agent url if the entity reference factory produces references to
  // entities backed by agents, otherwise the story id of the story entity
  // provider.
  const std::string agent_url_;

  EntityProviderRunner* const entity_provider_runner_;
  fidl::BindingSet<fuchsia::modular::EntityReferenceFactory> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(EntityReferenceFactoryImpl);
};

// This class provides |fuchsia::modular::Entity| implementations for a given
// data entity reference.
class EntityProviderRunner::DataEntity : fuchsia::modular::Entity {
 public:
  DataEntity(EntityProviderRunner* const provider, const std::string& entity_reference,
             std::map<std::string, std::string> data) {
    for (const auto& kv : data) {
      types_.push_back(kv.first);
    }
    data_ = std::move(data);

    bindings_.set_empty_set_handler(
        [provider, entity_reference] { provider->OnDataEntityFinished(entity_reference); });
  };

  void AddBinding(fidl::InterfaceRequest<fuchsia::modular::Entity> request) {
    bindings_.AddBinding(this, std::move(request));
  }

 private:
  // |fuchsia::modular::Entity|
  void GetTypes(GetTypesCallback result) override { result(types_); }
  // |fuchsia::modular::Entity|
  void GetData(std::string type, GetDataCallback result) override {
    auto it = data_.find(type);
    if (it != data_.end()) {
      fsl::SizedVmo vmo;
      FXL_CHECK(fsl::VmoFromString(it->second, &vmo));
      auto vmo_ptr = std::make_unique<fuchsia::mem::Buffer>(std::move(vmo).ToTransport());

      result(std::move(vmo_ptr));
    } else {
      result(nullptr);
    }
  }
  // |fuchsia::modular::Entity|
  void WriteData(std::string type, fuchsia::mem::Buffer data, WriteDataCallback callback) override {
    // TODO(MI4-1301)
    callback(fuchsia::modular::EntityWriteStatus::READ_ONLY);
  }
  // |fuchsia::modular::Entity|
  void GetReference(GetReferenceCallback callback) override {
    // TODO(MI4-1301)
    FXL_NOTIMPLEMENTED();
  }
  // |fuchsia::modular::Entity|
  void Watch(std::string type,
             fidl::InterfaceHandle<fuchsia::modular::EntityWatcher> watcher) override {
    // TODO(MI4-1301)
    FXL_NOTIMPLEMENTED();
  }

  std::vector<std::string> types_;
  std::map<std::string, std::string> data_;
  fidl::BindingSet<fuchsia::modular::Entity> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DataEntity);
};

EntityProviderRunner::EntityProviderRunner(EntityProviderLauncher* const entity_provider_launcher)
    : entity_provider_launcher_(entity_provider_launcher) {}

EntityProviderRunner::~EntityProviderRunner() = default;

void EntityProviderRunner::ConnectEntityReferenceFactory(
    const std::string& agent_url,
    fidl::InterfaceRequest<fuchsia::modular::EntityReferenceFactory> request) {
  auto it = entity_reference_factory_bindings_.find(agent_url);
  if (it == entity_reference_factory_bindings_.end()) {
    bool inserted;
    std::tie(it, inserted) = entity_reference_factory_bindings_.emplace(
        std::make_pair(agent_url, std::make_unique<EntityReferenceFactoryImpl>(agent_url, this)));
    FXL_DCHECK(inserted);
    it->second->set_empty_set_handler(
        [this, agent_url] { entity_reference_factory_bindings_.erase(agent_url); });
  }
  it->second->AddBinding(std::move(request));
}

std::string EntityProviderRunner::CreateStoryEntityReference(const std::string& story_id,
                                                             const std::string& cookie) {
  return EncodeStoryEntityReference(story_id, cookie);
}

void EntityProviderRunner::ConnectEntityResolver(
    fidl::InterfaceRequest<fuchsia::modular::EntityResolver> request) {
  entity_resolver_bindings_.AddBinding(this, std::move(request));
}

void EntityProviderRunner::OnEntityProviderFinished(const std::string agent_url) {
  entity_provider_controllers_.erase(agent_url);
}

void EntityProviderRunner::CreateReference(
    const std::string& agent_url, const std::string& cookie,
    fuchsia::modular::EntityReferenceFactory::CreateReferenceCallback callback) {
  auto entity_ref = EncodeAgentEntityReference(agent_url, cookie);
  callback(entity_ref);
}

void EntityProviderRunner::ResolveDataEntity(
    fidl::StringPtr entity_reference,
    fidl::InterfaceRequest<fuchsia::modular::Entity> entity_request) {
  std::map<std::string, std::string> entity_data;
  if (!DecodeEntityDataReference(entity_reference.value_or(""), &entity_data)) {
    FXL_LOG(INFO) << "Could not decode entity reference: " << entity_reference;
    return;
    // |entity_request| closes here.
  }

  auto inserted = data_entities_.emplace(std::make_pair(entity_reference.value_or(""), nullptr));
  if (inserted.second) {
    // This is a new entity.
    inserted.first->second =
        std::make_unique<DataEntity>(this, entity_reference.value_or(""), std::move(entity_data));
  }
  inserted.first->second->AddBinding(std::move(entity_request));
}

void EntityProviderRunner::OnDataEntityFinished(const std::string& entity_reference) {
  data_entities_.erase(entity_reference);
}

void EntityProviderRunner::ResolveEntity(
    std::string entity_reference, fidl::InterfaceRequest<fuchsia::modular::Entity> entity_request) {
  if (entity_reference.find(kEntityDataReferencePrefix) == 0ul) {
    ResolveDataEntity(entity_reference, std::move(entity_request));
    return;
  }

  std::string provider_uri;
  std::string cookie;
  std::string prefix;
  fuchsia::modular::EntityProviderPtr entity_provider;
  fuchsia::modular::AgentControllerPtr agent_controller;

  if (!DecodeEntityReference(entity_reference, &prefix, &provider_uri, &cookie)) {
    return;
    // |entity_request| is closed here.
  }

  bool is_story_entity = prefix == kStoryEntityReferencePrefix;

  if (is_story_entity) {
    entity_provider_launcher_->ConnectToStoryEntityProvider(provider_uri,
                                                            entity_provider.NewRequest());
  } else if (prefix == kEntityReferencePrefix) {
    entity_provider_launcher_->ConnectToEntityProvider(provider_uri, entity_provider.NewRequest(),
                                                       agent_controller.NewRequest());
  } else {
    return;
    // |entity_request| is closed here.
  }

  // Connect to the EntityProviderController managing this entity.
  auto it = entity_provider_controllers_.find(provider_uri);
  if (it == entity_provider_controllers_.end()) {
    bool inserted;
    std::tie(it, inserted) = entity_provider_controllers_.emplace(
        std::make_pair(provider_uri, std::make_unique<EntityProviderController>(
                                         std::move(entity_provider), std::move(agent_controller),
                                         [this, is_story_entity, provider_uri] {
                                           if (!is_story_entity) {
                                             OnEntityProviderFinished(provider_uri);
                                           }
                                         })));
    FXL_DCHECK(inserted);
  }

  it->second->ProvideEntity(cookie, entity_reference, std::move(entity_request));
}

}  // namespace modular
