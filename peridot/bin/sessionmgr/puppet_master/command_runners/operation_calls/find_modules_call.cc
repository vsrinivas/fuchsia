// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "peridot/bin/sessionmgr/puppet_master/command_runners/operation_calls/find_modules_call.h"

#include <lib/entity/cpp/json.h>
#include <lib/fostr/fidl/fuchsia/modular/formatting.h>
#include <lib/fsl/types/type_converters.h>
#include <lib/fsl/vmo/strings.h>
#include <src/lib/fxl/logging.h>

#include "peridot/bin/sessionmgr/puppet_master/command_runners/operation_calls/get_types_from_entity_call.h"
#include "peridot/lib/fidl/clone.h"

namespace modular {

namespace {

class FindModulesCall
    : public Operation<fuchsia::modular::ExecuteResult,
                       fuchsia::modular::FindModulesResponse> {
 public:
  FindModulesCall(fuchsia::modular::ModuleResolver* const module_resolver,
                  fuchsia::modular::EntityResolver* const entity_resolver,
                  fuchsia::modular::IntentPtr intent,
                  std::vector<std::string> requesting_module_path,
                  ResultCall result_call)
      : Operation("FindModulesCall", std::move(result_call)),
        module_resolver_(module_resolver),
        entity_resolver_(entity_resolver),
        intent_(std::move(intent)),
        requesting_module_path_(std::move(requesting_module_path)) {}

 private:
  void Run() override {
    FlowToken flow{this, &result_, &response_};
    // Default status. We'll update it and return if an error occurs.
    result_.status = fuchsia::modular::ExecuteStatus::OK;

    FXL_DCHECK(intent_->action) << intent_;

    constraint_futs_.reserve(intent_->parameters->size());

    resolver_query_.action = intent_->action;
    resolver_query_.handler = intent_->handler;
    resolver_query_.parameter_constraints.resize(0);

    for (auto& param : *intent_->parameters) {
      // TODO(MF-23): Deprecate parameter name nullability altogether.
      if (param.name.is_null() && intent_->handler.is_null()) {
        result_.error_message =
            "A null-named module parameter is not allowed "
            "when using fuchsia::modular::Intent.";
        result_.status = fuchsia::modular::ExecuteStatus::INVALID_COMMAND;
        return;
        // Operation finishes since |flow| goes out of scope.
      }

      // Skip processing null intent parameter names (these are generally
      // root/null link names).
      if (param.name.is_null()) {
        param.name = "";
      }

      constraint_futs_.push_back(
          GetTypesFromIntentParameter(std::move(param.data), param.name)
              ->Map([this, name = param.name](std::vector<std::string> types) {
                fuchsia::modular::FindModulesParameterConstraint constraint;
                constraint.param_name = name;
                constraint.param_types = std::move(types);
                return constraint;
              }));
    }

    Wait("FindModulesCall.Run.Wait", constraint_futs_)
        ->Then([this, flow](
                   std::vector<fuchsia::modular::FindModulesParameterConstraint>
                       constraint_params) {
          if (result_.status != fuchsia::modular::ExecuteStatus::OK) {
            // Operation finishes since |flow| goes out of scope.
            return;
          }
          resolver_query_.parameter_constraints = std::move(constraint_params);
          module_resolver_->FindModules(
              std::move(resolver_query_),
              [this, flow](fuchsia::modular::FindModulesResponse response) {
                response_ = std::move(response);
                // At this point, the only remaining |flow| is the one captured
                // in this lambda. This operation should end once |flow| goes
                // out of scope here.
              });
        });
  }

  // To avoid deadlocks, this function must not depend on anything that executes
  // on the story controller's operation queue.
  FuturePtr<std::vector<std::string>> GetTypesFromIntentParameter(
      fuchsia::modular::IntentParameterData input,
      const fidl::StringPtr& param_name) {
    auto fut = Future<std::vector<std::string>>::Create(
        "AddModCommandRunner::GetTypesFromIntentParameter");
    switch (input.Which()) {
      case fuchsia::modular::IntentParameterData::Tag::kEntityReference: {
        AddGetTypesFromEntityOperation(&operations_, entity_resolver_,
                                       input.entity_reference(),
                                       fut->Completer());
        break;
      }
      case fuchsia::modular::IntentParameterData::Tag::kEntityType: {
        fut->Complete(fidl::To<std::vector<std::string>>(input.entity_type()));
        break;
      }
      case fuchsia::modular::IntentParameterData::Tag::kJson: {
        std::string json_string;
        FXL_CHECK(fsl::StringFromVmo(input.json(), &json_string));
        if (auto result = GetTypesFromJson(json_string)) {
          fut->Complete(std::move(*result));
        } else {
          std::ostringstream stream;
          stream << "Mal-formed JSON in parameter: " << param_name;
          result_.error_message = stream.str();
          result_.status = fuchsia::modular::ExecuteStatus::INVALID_COMMAND;
          fut->Complete({});
        }
        break;
      }
      case fuchsia::modular::IntentParameterData::Tag::Invalid: {
        std::ostringstream stream;
        stream << "Invalid data for parameter with name: " << param_name;
        result_.error_message = stream.str();
        result_.status = fuchsia::modular::ExecuteStatus::INVALID_COMMAND;
        fut->Complete({});
        break;
      }
    }
    return fut;
  }

  std::optional<std::vector<std::string>> GetTypesFromJson(
      const fidl::StringPtr& input) {
    std::vector<std::string> types;
    if (ExtractEntityTypesFromJson(input, &types)) {
      return types;
    }
    return std::nullopt;
  }

  fuchsia::modular::ModuleResolver* const module_resolver_;  // Not Owned
  fuchsia::modular::EntityResolver* const entity_resolver_;  // Not owned.
  const fuchsia::modular::IntentPtr intent_;
  const std::vector<std::string> requesting_module_path_;

  fuchsia::modular::FindModulesQuery resolver_query_;
  std::vector<FuturePtr<fuchsia::modular::FindModulesParameterConstraint>>
      constraint_futs_;
  fuchsia::modular::ExecuteResult result_;
  fuchsia::modular::FindModulesResponse response_;
  OperationCollection operations_;
};

}  // namespace

void AddFindModulesOperation(
    OperationContainer* operation_container,
    fuchsia::modular::ModuleResolver* const module_resolver,
    fuchsia::modular::EntityResolver* const entity_resolver,
    fuchsia::modular::IntentPtr intent,
    std::vector<std::string> requesting_module_path,
    fit::function<void(fuchsia::modular::ExecuteResult,
                       fuchsia::modular::FindModulesResponse)>
        result_call) {
  operation_container->Add(std::make_unique<FindModulesCall>(
      module_resolver, entity_resolver, std::move(intent),
      std::move(requesting_module_path), std::move(result_call)));
}

}  // namespace modular
