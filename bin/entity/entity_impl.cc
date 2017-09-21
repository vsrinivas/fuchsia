// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/entity/entity_impl.h"

#include "apps/modular/src/entity/entity_repository.h"

namespace modular {

EntityImpl::EntityImpl(std::string reference,
                       fidl::Array<fidl::String> types,
                       fidl::Array<fidl::Array<uint8_t>> contents) {
  reference_ = EntityReference::New();
  reference_->internal_value = reference;

  for (size_t i = 0; i < types.size(); i++) {
    type_to_contents_[types[i]] = std::move(contents[i]);
  }
}

EntityImpl::~EntityImpl() = default;

void EntityImpl::AddBinding(fidl::InterfaceRequest<Entity> entity_request) {
  bindings_.AddBinding(this, std::move(entity_request));
}

void EntityImpl::GetReference(const GetReferenceCallback& callback) {
  callback(reference_.Clone());
}

void EntityImpl::GetTypes(const GetTypesCallback& types_callback) {
  fidl::Array<fidl::String> types;
  for (auto& it : type_to_contents_) {
    types.push_back(it.first);
  }
  types_callback(std::move(types));
}

void EntityImpl::GetContent(const fidl::String& type,
                            const GetContentCallback& content_callback) {
  auto it = type_to_contents_.find(type);
  if (it == type_to_contents_.end()) {
    content_callback(nullptr);
  } else {
    content_callback(it->second.Clone());
  }
}

}  // namespace modular
