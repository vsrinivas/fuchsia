// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_ENTITY_ENTITY_REPOSITORY_H_
#define PERIDOT_BIN_ENTITY_ENTITY_REPOSITORY_H_

#include <unordered_map>

#include "lib/async/cpp/operation.h"
#include "lib/entity/fidl/entity.fidl.h"
#include "lib/entity/fidl/entity_resolver.fidl.h"
#include "lib/entity/fidl/entity_store.fidl.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/string.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/entity/entity_impl.h"

namespace modular {

// A simple "entity provider" for creating Entities made of bags of bytes
// with associated types. This class also provides a way to create, reference
// and dereference |Entity|s. See |EntityImpl| for an actual implementation
// of the |Entity| interface.
// TODO(vardhan): Persist entity references and data on the ledger.
class EntityRepository : EntityStore, EntityResolver {
 public:
  EntityRepository();
  ~EntityRepository() override;

  void ConnectEntityStore(fidl::InterfaceRequest<EntityStore> request);
  void ConnectEntityResolver(fidl::InterfaceRequest<EntityResolver> request);

 private:
  class CreateEntityCall;

  // |EntityStore|
  void CreateEntity(fidl::Array<fidl::String> types,
                    fidl::Array<fidl::Array<uint8_t>> contents,
                    fidl::InterfaceRequest<Entity> request) override;

  // |EntityResolver|
  void GetEntity(EntityReferencePtr reference,
                 fidl::InterfaceRequest<Entity> request) override;

  using ReferenceMap =
      std::unordered_map<std::string, std::unique_ptr<EntityImpl>>;
  ReferenceMap ref_to_entity_;

  fidl::BindingSet<EntityStore> entity_store_bindings_;
  fidl::BindingSet<EntityResolver> entity_resolver_bindings_;

  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(EntityRepository);
};

}  // namespace modular

#endif  // PERIDOT_BIN_ENTITY_ENTITY_REPOSITORY_H_
