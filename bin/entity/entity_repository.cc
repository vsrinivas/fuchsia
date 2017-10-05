// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/entity/entity_repository.h"

#include "lib/fxl/macros.h"
#include "lib/fxl/random/rand.h"
#include "peridot/bin/entity/entity_impl.h"
#include "lib/async/cpp/operation.h"

namespace modular {

class EntityRepository::CreateEntityCall : Operation<> {
 public:
  CreateEntityCall(OperationContainer* container,
                   EntityRepository* entity_repository,
                   fidl::Array<fidl::String> types,
                   fidl::Array<fidl::Array<uint8_t>> contents,
                   fidl::InterfaceRequest<Entity> entity_request)
      : Operation("EntityRepository::CreateEntityCall", container, [] {}),
        entity_repository_(entity_repository),
        types_(std::move(types)),
        contents_(std::move(contents)),
        entity_request_(std::move(entity_request)) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow(this);

    if (types_.size() != contents_.size()) {
      FXL_DLOG(WARNING)
          << "Cannot create entity: size of types != size of contents";
      return;
      // |entity_request| closes at this point to indicate error.
    }

    std::string new_ref = GenerateNewReference();
    entity_repository_->ref_to_entity_.emplace(
        new_ref, std::make_unique<EntityImpl>(new_ref, std::move(types_),
                                              std::move(contents_)));
    entity_repository_->ref_to_entity_[new_ref]->AddBinding(
        std::move(entity_request_));
  }

  // This generates an unused entity reference based on a 64-bit random int,
  // encoded as a string.
  std::string GenerateNewReference() const {
    std::string retval;
    do {
      retval = std::to_string(fxl::RandUint64());
    } while (entity_repository_->ref_to_entity_.find(retval) !=
             entity_repository_->ref_to_entity_.end());
    return retval;
  }

  EntityRepository* const entity_repository_;
  fidl::Array<fidl::String> types_;
  fidl::Array<fidl::Array<uint8_t>> contents_;
  fidl::InterfaceRequest<Entity> entity_request_;
};

EntityRepository::EntityRepository() = default;
EntityRepository::~EntityRepository() = default;

void EntityRepository::ConnectEntityStore(
    fidl::InterfaceRequest<EntityStore> request) {
  entity_store_bindings_.AddBinding(this, std::move(request));
}

void EntityRepository::ConnectEntityResolver(
    fidl::InterfaceRequest<EntityResolver> request) {
  entity_resolver_bindings_.AddBinding(this, std::move(request));
}

// |EntityStore|
void EntityRepository::CreateEntity(fidl::Array<fidl::String> types,
                                    fidl::Array<fidl::Array<uint8_t>> contents,
                                    fidl::InterfaceRequest<Entity> request) {
  new CreateEntityCall(&operation_queue_, this, std::move(types),
                       std::move(contents), std::move(request));
}

// |EntityResolver|
void EntityRepository::GetEntity(EntityReferencePtr reference,
                                 fidl::InterfaceRequest<Entity> request) {
  auto it = ref_to_entity_.find(reference->internal_value);
  if (it == ref_to_entity_.end()) {
    return;
    // |entity_request| closes at this point to indicate error.
  }

  it->second->AddBinding(std::move(request));
}

}  // namespace modular
