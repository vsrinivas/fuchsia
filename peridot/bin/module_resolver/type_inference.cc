// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/module_resolver/type_inference.h"

#include <string>
#include <vector>

#include <lib/async/cpp/operation.h>
#include <lib/entity/cpp/json.h>
#include <lib/fsl/types/type_converters.h>
#include <lib/fxl/macros.h>
#include <lib/fxl/type_converter.h>

namespace modular {

ParameterTypeInferenceHelper::ParameterTypeInferenceHelper(
    fuchsia::modular::EntityResolverPtr entity_resolver)
    : entity_resolver_(std::move(entity_resolver)) {}

ParameterTypeInferenceHelper::~ParameterTypeInferenceHelper() = default;

class ParameterTypeInferenceHelper::GetParameterTypesCall
    : public Operation<std::vector<std::string>> {
 public:
  GetParameterTypesCall(fuchsia::modular::EntityResolver* entity_resolver,
                        const fidl::StringPtr& entity_reference,
                        ResultCall result)
      : Operation("ParameterTypeInferenceHelper::GetParameterTypesCall",
                  std::move(result)),
        entity_resolver_(entity_resolver),
        entity_reference_(entity_reference) {}

  void Run() {
    entity_resolver_->ResolveEntity(entity_reference_, entity_.NewRequest());
    entity_->GetTypes([this](const fidl::VectorPtr<fidl::StringPtr>& types) {
      Done(fxl::To<std::vector<std::string>>(types));
    });
  }

 private:
  fuchsia::modular::EntityResolver* const entity_resolver_;
  fidl::StringPtr const entity_reference_;
  fuchsia::modular::EntityPtr entity_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GetParameterTypesCall);
};

void ParameterTypeInferenceHelper::GetParameterTypes(
    const fuchsia::modular::ResolverParameterConstraint& parameter_constraint,
    const std::function<void(std::vector<std::string>)>& result_callback) {
  if (parameter_constraint.is_entity_type()) {
    result_callback(
        std::vector<std::string>(parameter_constraint.entity_type()->begin(),
                                 parameter_constraint.entity_type()->end()));
  } else if (parameter_constraint.is_json()) {
    std::vector<std::string> types;
    if (!ExtractEntityTypesFromJson(parameter_constraint.json(), &types)) {
      FXL_LOG(WARNING) << "Mal-formed JSON in parameter: "
                       << parameter_constraint.json();
      result_callback({});
    } else {
      result_callback(types);
    }
  } else if (parameter_constraint.is_entity_reference()) {
    operation_collection_.Add(new GetParameterTypesCall(
        entity_resolver_.get(), parameter_constraint.entity_reference(),
        result_callback));
  } else if (parameter_constraint.is_link_info()) {
    if (parameter_constraint.link_info().allowed_types) {
      std::vector<std::string> types(
          parameter_constraint.link_info()
              .allowed_types->allowed_entity_types->begin(),
          parameter_constraint.link_info()
              .allowed_types->allowed_entity_types->end());
      result_callback(std::move(types));
    } else if (parameter_constraint.link_info().content_snapshot) {
      // TODO(thatguy): See if there's an fuchsia::modular::Entity reference on
      // the fuchsia::modular::Link. If so, get the types from that.  If
      // resolution results in a Module being started, this
      // fuchsia::modular::Link should have its allowed types constrained, since
      // *another* Module is now relying on a small set of types being set.
      // Consider doing this when we move type extraction to the Framework and
      // simplify the Resolver.
      std::string entity_reference;
      if (EntityReferenceFromJson(
              parameter_constraint.link_info().content_snapshot,
              &entity_reference)) {
        operation_collection_.Add(new GetParameterTypesCall(
            entity_resolver_.get(), entity_reference, result_callback));
      }
    }
  } else {
    FXL_NOTREACHED();
  }
}

}  // namespace modular
