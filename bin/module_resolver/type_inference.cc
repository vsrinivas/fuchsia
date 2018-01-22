// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include "lib/async/cpp/operation.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/module_resolver/type_inference.h"
#include "peridot/public/lib/entity/cpp/json.h"

namespace maxwell {

NounTypeInferenceHelper::NounTypeInferenceHelper(
    modular::EntityResolverPtr entity_resolver)
    : entity_resolver_(std::move(entity_resolver)) {}

NounTypeInferenceHelper::~NounTypeInferenceHelper() = default;

class NounTypeInferenceHelper::GetNounTypesCall
    : public modular::Operation<std::vector<std::string>> {
 public:
  GetNounTypesCall(modular::OperationContainer* container,
                   modular::EntityResolver* entity_resolver,
                   const fidl::String& entity_reference,
                   ResultCall result)
      : Operation("NounTypeInferenceHelper::GetNounTypesCall",
                  container,
                  std::move(result)),
        entity_resolver_(entity_resolver),
        entity_reference_(entity_reference) {
    Ready();
  }

  void Run() {
    entity_resolver_->ResolveEntity(entity_reference_, entity_.NewRequest());
    entity_->GetTypes([this](const fidl::Array<fidl::String>& types) {
      Done(types.To<std::vector<std::string>>());
    });
  }

 private:
  modular::EntityResolver* const entity_resolver_;
  fidl::String const entity_reference_;
  modular::EntityPtr entity_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GetNounTypesCall);
};

void NounTypeInferenceHelper::GetNounTypes(
    const modular::ResolverNounConstraintPtr& noun_constraint,
    const std::function<void(std::vector<std::string>)>& result_callback) {
  if (noun_constraint->is_entity_type()) {
    result_callback(
        std::vector<std::string>(noun_constraint->get_entity_type().begin(),
                                 noun_constraint->get_entity_type().end()));
  } else if (noun_constraint->is_json()) {
    std::vector<std::string> types;
    if (!modular::ExtractEntityTypesFromJson(noun_constraint->get_json(),
                                             &types)) {
      FXL_LOG(WARNING) << "Mal-formed JSON in noun: "
                       << noun_constraint->get_json();
      result_callback({});
    } else {
      result_callback(types);
    }
  } else if (noun_constraint->is_entity_reference()) {
    new GetNounTypesCall(&operation_collection_, entity_resolver_.get(),
                         noun_constraint->get_entity_reference(),
                         result_callback);
  } else if (noun_constraint->is_link_info()) {
    if (noun_constraint->get_link_info()->allowed_types) {
      std::vector<std::string> types(
          noun_constraint->get_link_info()
              ->allowed_types->allowed_entity_types.begin(),
          noun_constraint->get_link_info()
              ->allowed_types->allowed_entity_types.end());
      result_callback(std::move(types));
    } else if (noun_constraint->get_link_info()->content_snapshot) {
      // TODO(thatguy): See if there's an Entity reference on the Link. If so,
      // get the types from that.  If resolution results in a Module being
      // started, this Link should have its allowed types constrained, since
      // *another* Module is now relying on a small set of types being set.
      // Consider doing this when we move type extraction to the Framework and
      // simplify the Resolver.
      std::string entity_reference;
      if (modular::EntityReferenceFromJson(
              noun_constraint->get_link_info()->content_snapshot,
              &entity_reference)) {
        new GetNounTypesCall(&operation_collection_, entity_resolver_.get(),
                             entity_reference, result_callback);
      }
    }
  } else {
    FXL_NOTREACHED();
  }
}

}  // namespace maxwell
