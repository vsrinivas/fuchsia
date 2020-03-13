// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/testing/entity_resolver_fake.h"

#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/syslog/cpp/logger.h"

namespace modular {

class EntityResolverFake::EntityImpl : fuchsia::modular::Entity {
 public:
  EntityImpl(std::map<std::string, std::string> types_and_data) : types_and_data_(types_and_data) {}

  void Connect(fidl::InterfaceRequest<fuchsia::modular::Entity> request) {
    bindings_.AddBinding(this, std::move(request));
  }

 private:
  // |fuchsia::modular::Entity|
  void GetTypes(GetTypesCallback callback) override {
    std::vector<std::string> types;
    for (const auto& entry : types_and_data_) {
      types.push_back(entry.first);
    }
    callback(std::move(types));
  }

  // |fuchsia::modular::Entity|
  void GetData(std::string type, GetDataCallback callback) override {
    auto it = types_and_data_.find(type);
    if (it == types_and_data_.end()) {
      callback(nullptr);
      return;
    }
    fsl::SizedVmo vmo;
    FX_CHECK(fsl::VmoFromString(it->second, &vmo));
    auto vmo_ptr = std::make_unique<fuchsia::mem::Buffer>(std::move(vmo).ToTransport());

    callback(std::move(vmo_ptr));
  }

  // |fuchsia::modular::Entity|
  void WriteData(std::string type, fuchsia::mem::Buffer data, WriteDataCallback callback) override {
    // TODO(rosswang)
    callback(fuchsia::modular::EntityWriteStatus::READ_ONLY);
  }

  // |fuchsia::modular::Entity|
  void GetReference(GetReferenceCallback callback) override {
    // TODO(rosswang)
    FXL_NOTIMPLEMENTED();
  }

  // |fuchsia::modular::Entity|
  void Watch(std::string type,
             fidl::InterfaceHandle<fuchsia::modular::EntityWatcher> watcher) override {
    // TODO(MI4-1301)
    FXL_NOTIMPLEMENTED();
  }

  std::map<std::string, std::string> types_and_data_;

  fidl::BindingSet<fuchsia::modular::Entity> bindings_;
};

EntityResolverFake::EntityResolverFake() = default;
EntityResolverFake::~EntityResolverFake() = default;

void EntityResolverFake::Connect(fidl::InterfaceRequest<fuchsia::modular::EntityResolver> request) {
  bindings_.AddBinding(this, std::move(request));
}

// Returns an fuchsia::modular::Entity reference that will resolve to an
// fuchsia::modular::Entity. |types_and_data| is a map of data type to data
// bytes.
fidl::StringPtr EntityResolverFake::AddEntity(std::map<std::string, std::string> types_and_data) {
  const std::string id = std::to_string(next_entity_id_++);

  auto entity = std::make_unique<EntityImpl>(std::move(types_and_data));
  entities_.emplace(id, std::move(entity));
  return id;
}

void EntityResolverFake::ResolveEntity(
    std::string entity_reference, fidl::InterfaceRequest<fuchsia::modular::Entity> entity_request) {
  auto it = entities_.find(entity_reference);
  if (it == entities_.end()) {
    return;  // |entity_request| is reset here.
  }

  it->second->Connect(std::move(entity_request));
}

}  // namespace modular
