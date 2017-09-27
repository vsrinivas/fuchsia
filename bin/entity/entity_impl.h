// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_ENTITY_ENTITY_IMPL_H_
#define PERIDOT_BIN_ENTITY_ENTITY_IMPL_H_

#include <string>
#include <unordered_map>

#include "lib/entity/fidl/entity.fidl.h"
#include "lib/entity/fidl/entity_reference.fidl.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/string.h"
#include "lib/fxl/macros.h"

namespace modular {

// Implements the |Entity| interface, which provides a list of types and
// respective contents for each type. This implementation is used by
// EntityRepository.
class EntityImpl : Entity {
 public:
  EntityImpl(std::string reference,
             fidl::Array<fidl::String> types,
             fidl::Array<fidl::Array<uint8_t>> contents);

  ~EntityImpl() override;

  void AddBinding(fidl::InterfaceRequest<Entity> entity_request);

 private:
  // |Entity|
  void GetReference(const GetReferenceCallback& callback) override;
  // |Entity|
  void GetTypes(const GetTypesCallback& types) override;
  // |Entity|
  void GetContent(const fidl::String& type,
                  const GetContentCallback& content) override;

  EntityReferencePtr reference_;
  std::unordered_map<std::string, fidl::Array<uint8_t>> type_to_contents_;
  fidl::BindingSet<Entity> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(EntityImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_ENTITY_ENTITY_IMPL_H_
