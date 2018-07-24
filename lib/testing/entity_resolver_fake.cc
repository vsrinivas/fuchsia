// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/testing/entity_resolver_fake.h"

#include <lib/fsl/vmo/strings.h>

namespace modular {

class EntityResolverFake::EntityImpl : fuchsia::modular::Entity {
 public:
  EntityImpl(std::map<std::string, std::string> types_and_data)
      : types_and_data_(types_and_data) {}

  void Connect(fidl::InterfaceRequest<fuchsia::modular::Entity> request) {
    bindings_.AddBinding(this, std::move(request));
  }

 private:
  // |fuchsia::modular::Entity|
  void GetTypes(GetTypesCallback callback) override {
    fidl::VectorPtr<fidl::StringPtr> types;
    for (const auto& entry : types_and_data_) {
      types.push_back(entry.first);
    }
    callback(std::move(types));
  }

  // |fuchsia::modular::Entity|
  void GetData(fidl::StringPtr type, GetDataCallback callback) override {
    auto it = types_and_data_.find(type);
    if (it == types_and_data_.end()) {
      callback(nullptr);
      return;
    }
    fsl::SizedVmo vmo;
    FXL_CHECK(fsl::VmoFromString(it->second, &vmo));
    auto vmo_ptr =
        std::make_unique<fuchsia::mem::Buffer>(std::move(vmo).ToTransport());

    callback(std::move(vmo_ptr));
  }

  std::map<std::string, std::string> types_and_data_;

  fidl::BindingSet<fuchsia::modular::Entity> bindings_;
};

EntityResolverFake::EntityResolverFake() = default;
EntityResolverFake::~EntityResolverFake() = default;

void EntityResolverFake::Connect(
    fidl::InterfaceRequest<fuchsia::modular::EntityResolver> request) {
  bindings_.AddBinding(this, std::move(request));
}

// Returns an fuchsia::modular::Entity reference that will resolve to an
// fuchsia::modular::Entity. |types_and_data| is a map of data type to data
// bytes.
fidl::StringPtr EntityResolverFake::AddEntity(
    std::map<std::string, std::string> types_and_data) {
  const std::string id = std::to_string(next_entity_id_++);

  auto entity = std::make_unique<EntityImpl>(std::move(types_and_data));
  entities_.emplace(id, std::move(entity));
  return id;
}

void EntityResolverFake::ResolveEntity(
    fidl::StringPtr entity_reference,
    fidl::InterfaceRequest<fuchsia::modular::Entity> entity_request) {
  auto it = entities_.find(entity_reference);
  if (it == entities_.end()) {
    return;  // |entity_request| is reset here.
  }

  it->second->Connect(std::move(entity_request));
}

}  // namespace modular
