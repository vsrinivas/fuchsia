// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "peridot/bin/user_runner/puppet_master/command_runners/operation_calls/find_modules_call.h"

#include <lib/entity/cpp/json.h>
#include <lib/fsl/types/type_converters.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/type_converter.h>
#include "peridot/bin/user_runner/puppet_master/command_runners/operation_calls/get_link_path_for_parameter_name_call.h"
#include "peridot/bin/user_runner/puppet_master/command_runners/operation_calls/get_types_from_entity_call.h"
#include "peridot/lib/fidl/clone.h"

namespace modular {

FindModulesCall::FindModulesCall(
    StoryStorage* const story_storage,
    fuchsia::modular::ModuleResolver* const module_resolver,
    fuchsia::modular::EntityResolver* const entity_resolver,
    fuchsia::modular::IntentPtr intent,
    fidl::VectorPtr<fidl::StringPtr> requesting_module_path,
    ResultCall result_call)
    : Operation("FindModulesCall", std::move(result_call)),
      story_storage_(story_storage),
      module_resolver_(module_resolver),
      entity_resolver_(entity_resolver),
      intent_(std::move(intent)),
      requesting_module_path_(std::move(requesting_module_path)) {}

void FindModulesCall::Run() {
  FlowToken flow{this, &result_, &response_};
  // Default status. We'll update it and return if an error occurs.
  result_.status = fuchsia::modular::ExecuteStatus::OK;

  if (intent_->handler) {
    // We already know which module to use, but we need its manifest.
    module_resolver_->GetModuleManifest(
        intent_->handler,
        [this, flow](fuchsia::modular::ModuleManifestPtr manifest) {
          fuchsia::modular::FindModulesResult result;
          result.module_id = intent_->handler;
          result.manifest = CloneOptional(manifest);
          response_.results.push_back(std::move(result));
          // Operation finshes since |flow| goes out of scope.
        });
    return;
  }

  FXL_DCHECK(intent_->action);

  constraint_futs_.reserve(intent_->parameters->size());

  resolver_query_.action = intent_->action;
  resolver_query_.parameter_constraints.resize(0);
  resolver_query_.parameter_constraints->reserve(intent_->parameters->size());

  for (const auto& param : *intent_->parameters) {
    if (param.name.is_null() && intent_->handler.is_null()) {
      // It is not allowed to have a null intent name (left in for backwards
      // compatibility with old code: MI4-736) and rely on action-based
      // resolution.
      result_.error_message =
          "A null-named module parameter is not allowed "
          "when using fuchsia::modular::Intent.action.";
      result_.status = fuchsia::modular::ExecuteStatus::INVALID_COMMAND;
      return;
      // Operation finishes since |flow| goes out of scope.
    }

    constraint_futs_.push_back(
        GetTypesFromIntentParameter(requesting_module_path_.Clone(), param.data,
                                    param.name)
            ->Map([this, name = param.name](std::vector<std::string> types) {
              fuchsia::modular::FindModulesParameterConstraint constraint;
              constraint.param_name = name;
              constraint.param_types =
                  fxl::To<fidl::VectorPtr<fidl::StringPtr>>(std::move(types));
              return constraint;
            }));
  }

  Wait("AddModCommandRunner.FindModulesCall.Run.Wait", constraint_futs_)
      ->Then([this, flow](
                 std::vector<fuchsia::modular::FindModulesParameterConstraint>
                     constraint_params) {
        if (result_.status != fuchsia::modular::ExecuteStatus::OK) {
          // Operation finishes since |flow| goes out of scope.
          return;
        }
        resolver_query_.parameter_constraints.reset(
            std::move(constraint_params));
        module_resolver_->FindModules(
            std::move(resolver_query_),
            [this, flow](fuchsia::modular::FindModulesResponse response) {
              response_ = std::move(response);
              // This operation should end once |flow| goes out of scope
              // here.
            });
      });
}

FuturePtr<std::vector<std::string>>
FindModulesCall::GetTypesFromIntentParameter(
    fidl::VectorPtr<fidl::StringPtr> module_path,
    const fuchsia::modular::IntentParameterData& input,
    const fidl::StringPtr& param_name) {
  auto fut = Future<std::vector<std::string>>::Create(
      "AddModCommandRunner::GetTypesFromIntentParameter");
  switch (input.Which()) {
    case fuchsia::modular::IntentParameterData::Tag::kEntityReference: {
      operations_.Add(new GetTypesFromEntityCall(
          entity_resolver_, input.entity_reference(), fut->Completer()));
      break;
    }
    case fuchsia::modular::IntentParameterData::Tag::kEntityType: {
      fut->Complete(std::vector<std::string>(input.entity_type()->begin(),
                                             input.entity_type()->end()));
      break;
    }
    case fuchsia::modular::IntentParameterData::Tag::kJson: {
      std::string json_string;
      FXL_CHECK(fsl::StringFromVmo(input.json(), &json_string));
      auto result = GetTypesFromJson(json_string);
      if (result.first) {
        fut->Complete(std::move(result.second));
      } else {
        std::ostringstream stream;
        stream << "Mal-formed JSON in parameter: " << param_name;
        result_.error_message = stream.str();
        result_.status = fuchsia::modular::ExecuteStatus::INVALID_COMMAND;
        fut->Complete({});
      }
      break;
    }
    case fuchsia::modular::IntentParameterData::Tag::kLinkName: {
      auto did_get_lp = Future<fuchsia::modular::LinkPathPtr>::Create(
          "AddModCommandRunner::GetTypesFromIntentParameter.did_get_lp");
      operations_.Add(new GetLinkPathForParameterNameCall(
          story_storage_, module_path.Clone(), input.link_name(),
          did_get_lp->Completer()));
      did_get_lp->Then([this, fut,
                        param_name](fuchsia::modular::LinkPathPtr lp) {
        if (!lp) {
          std::ostringstream stream;
          stream << "No link path found for parameter with name " << param_name;
          result_.error_message = stream.str();
          result_.status = fuchsia::modular::ExecuteStatus::INVALID_COMMAND;
          fut->Complete({});
        } else {
          // If the call below has some error it will be set in result_.
          GetTypesFromLink(std::move(lp), fut->Completer(), param_name);
        }
      });
      break;
    }
    case fuchsia::modular::IntentParameterData::Tag::kLinkPath: {
      LinkPathPtr lp = CloneOptional(input.link_path());
      // If the call below has some error it will be set in result_.
      GetTypesFromLink(std::move(lp), fut->Completer(), param_name);
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

std::pair<bool, std::vector<std::string>> FindModulesCall::GetTypesFromJson(
    const fidl::StringPtr& input) {
  std::vector<std::string> types;
  if (!ExtractEntityTypesFromJson(input, &types)) {
    return {false, {}};
  } else {
    return {true, types};
  }
}

void FindModulesCall::GetTypesFromLink(
    fuchsia::modular::LinkPathPtr link_path,
    std::function<void(std::vector<std::string>)> done,
    const fidl::StringPtr& param_name) {
  story_storage_->GetLinkValue(*link_path)
      ->Then([this, done = std::move(done), param_name](
                 StoryStorage::Status status, fidl::StringPtr v) {
        if (status != StoryStorage::Status::OK) {
          std::ostringstream stream;
          stream << "StoryStorage failed with status: " << (uint32_t)status
                 << " for parameter with name " << param_name;
          result_.error_message = stream.str();
          result_.status = fuchsia::modular::ExecuteStatus::INTERNAL_ERROR;
          done({});
          return;
        }
        auto result = GetTypesFromJson(v);
        if (!result.first) {
          std::ostringstream stream;
          stream << "Mal-formed JSON read from link for parameter: "
                 << param_name;
          result_.error_message = stream.str();
          result_.status = fuchsia::modular::ExecuteStatus::INTERNAL_ERROR;
        }
        done(result.second);
      });
}

}  // namespace modular
