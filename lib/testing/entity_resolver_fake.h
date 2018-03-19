// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_TESTING_ENTITY_RESOLVER_FAKE_H_
#define PERIDOT_LIB_TESTING_ENTITY_RESOLVER_FAKE_H_

#include <map>
#include <memory>
#include <string>

#include "lib/entity/fidl/entity_resolver.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/string.h"
#include "lib/fxl/macros.h"

namespace modular {

class EntityResolverFake : public EntityResolver {
 public:
  EntityResolverFake();
  ~EntityResolverFake() override;

  void Connect(f1dl::InterfaceRequest<EntityResolver> request);

  // Returns an Entity reference that will resolve to an Entity.
  // |types_and_data| is a map of data type to data bytes.
  f1dl::StringPtr AddEntity(std::map<std::string, std::string> types_and_data);

 private:
  class EntityImpl;

  void ResolveEntity(const f1dl::StringPtr& entity_reference,
                     f1dl::InterfaceRequest<Entity> entity_request) override;

  int next_entity_id_{0};
  std::map<std::string, std::unique_ptr<EntityImpl>> entities_;
  f1dl::BindingSet<EntityResolver> bindings_;
};

}  // namespace modular

#endif  // PERIDOT_LIB_TESTING_ENTITY_RESOLVER_FAKE_H_
