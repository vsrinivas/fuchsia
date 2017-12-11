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
    const modular::NounPtr& noun,
    const std::function<void(std::vector<std::string>)>& result_callback) {
  if (noun->is_entity_type()) {
    result_callback(std::vector<std::string>(noun->get_entity_type().begin(),
                                             noun->get_entity_type().end()));
  } else if (noun->is_json()) {
    std::vector<std::string> types;
    if (!modular::ExtractEntityTypesFromJson(noun->get_json(), &types)) {
      FXL_LOG(WARNING) << "Mal-formed JSON in noun: " << noun->get_json();
      result_callback({});
    } else {
      result_callback(types);
    }
  } else if (noun->is_entity_reference()) {
    new GetNounTypesCall(&operation_collection_, entity_resolver_.get(),
                         noun->get_entity_reference(), result_callback);
  } else {
    FXL_NOTREACHED();
  }
}

}  // namespace maxwell
