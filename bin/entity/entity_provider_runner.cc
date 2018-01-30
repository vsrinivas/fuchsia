// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(vardhan): Make entity references secure (no introspection allowed).

#include "peridot/bin/entity/entity_provider_runner.h"

#include <utility>

#include "lib/fxl/macros.h"
#include "lib/fxl/strings/join_strings.h"
#include "peridot/bin/entity/entity_provider_controller.h"
#include "peridot/lib/fidl/json_xdr.h"
#include "peridot/lib/util/string_escape.h"

namespace modular {
namespace {

constexpr char kEntityReferencePrefix[] = "EntityRef";
constexpr char kEntityDataReferencePrefix[] = "EntityData";
constexpr size_t kDataEntityMaxByteSize = 1024 * 16;

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

std::string EncodeEntityDataReference(
    fidl::Map<fidl::String, fidl::String>* const type_to_data) {
  std::string encoded;
  XdrWrite(&encoded, type_to_data,
           XdrFilter<fidl::Map<fidl::String, fidl::String>>);

  std::vector<std::string> parts(2);
  parts[0] = kEntityDataReferencePrefix;
  parts[1] = StringEscape(encoded, "/");
  return fxl::JoinStrings(parts, "/");
}

bool DecodeEntityDataReference(const std::string& entity_reference,
                               std::map<std::string, std::string>* data) {
  auto parts = SplitEscapedString(entity_reference, '/');
  if (parts.size() != 2 ||
      StringUnescape(parts[0]) != kEntityDataReferencePrefix) {
    return false;
  }

  return XdrRead(StringUnescape(parts[1].ToString()), data,
                 XdrFilter<std::map<std::string, std::string>>);
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

  void set_empty_set_handler(const std::function<void()>& handler) {
    bindings_.set_empty_set_handler(handler);
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

// This class provides |Entity| implementations for a given data entity
// reference.
class EntityProviderRunner::DataEntity : Entity {
 public:
  DataEntity(EntityProviderRunner* const provider,
             const std::string& entity_reference,
             std::map<std::string, std::string> data) {
    for (const auto& kv : data) {
      types_.push_back(kv.first);
    }
    data_ = std::move(data);

    bindings_.set_empty_set_handler([provider, entity_reference] {
      provider->OnDataEntityFinished(entity_reference);
    });
  };

  void AddBinding(fidl::InterfaceRequest<Entity> request) {
    bindings_.AddBinding(this, std::move(request));
  }

 private:
  // |Entity|
  void GetTypes(const GetTypesCallback& result) {
    result(fidl::Array<fidl::String>::From<>(types_));
  }
  // |Entity|
  void GetData(const fidl::String& type, const GetDataCallback& result) {
    auto it = data_.find(type);
    if (it != data_.end()) {
      result(it->second);
    } else {
      result(nullptr);
    }
  }

  std::vector<std::string> types_;
  std::map<std::string, std::string> data_;
  fidl::BindingSet<Entity> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DataEntity);
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
    it->second->set_empty_set_handler([this, agent_url] {
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

fidl::String EntityProviderRunner::CreateReferenceFromData(
    fidl::Map<fidl::String, fidl::String>* const type_to_data) {
  size_t total_bytes = 0;
  for (const auto& it : *type_to_data) {
    total_bytes += it.GetKey().size() + it.GetValue().size();
  }
  if (total_bytes > kDataEntityMaxByteSize) {
    FXL_LOG(ERROR)
        << "Could not create entity data reference: size was to big ("
        << total_bytes << " bytes)";
    return nullptr;
  }
  return EncodeEntityDataReference(type_to_data);
}

void EntityProviderRunner::CreateReference(
    const std::string& agent_url,
    const fidl::String& cookie,
    const EntityReferenceFactory::CreateReferenceCallback& callback) {
  auto entity_ref = EncodeEntityReference(agent_url, cookie);
  callback(entity_ref);
}

void EntityProviderRunner::ResolveDataEntity(
    const fidl::String& entity_reference,
    fidl::InterfaceRequest<Entity> entity_request) {
  std::map<std::string, std::string> entity_data;
  if (!DecodeEntityDataReference(entity_reference, &entity_data)) {
    FXL_LOG(INFO) << "Could not decode entity reference: " << entity_reference;
    return;
    // |entity_request| closes here.
  }

  auto inserted =
      data_entities_.emplace(std::make_pair(entity_reference.get(), nullptr));
  if (inserted.second) {
    // This is a new entity.
    inserted.first->second = std::make_unique<DataEntity>(
        this, entity_reference.get(), std::move(entity_data));
  }
  inserted.first->second->AddBinding(std::move(entity_request));
}

void EntityProviderRunner::OnDataEntityFinished(
    const std::string& entity_reference) {
  data_entities_.erase(entity_reference);
}

void EntityProviderRunner::ResolveEntity(
    const fidl::String& entity_reference,
    fidl::InterfaceRequest<Entity> entity_request) {
  if (entity_reference.get().find(kEntityDataReferencePrefix) == 0ul) {
    ResolveDataEntity(entity_reference, std::move(entity_request));
    return;
  }

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
    std::tie(it, inserted) =
        entity_provider_controllers_.emplace(std::make_pair(
            agent_url, std::make_unique<EntityProviderController>(
                           this, entity_provider_launcher_, agent_url)));
    FXL_DCHECK(inserted);
  }

  it->second->ProvideEntity(cookie, std::move(entity_request));
}

}  // namespace modular
