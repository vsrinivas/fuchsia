// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "peridot/bin/user_runner/puppet_master/command_runners/operation_calls/find_modules_call.h"

#include <lib/entity/cpp/json.h>
#include <lib/fsl/types/type_converters.h>
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
  FlowToken flow{this, &response_};
  if (intent_->handler) {
    // We already know which module to use, but we need its manifest.
    module_resolver_->GetModuleManifest(
        intent_->handler,
        [this, flow](fuchsia::modular::ModuleManifestPtr manifest) {
          fuchsia::modular::FindModulesResult result;
          result.module_id = intent_->handler;
          result.manifest = CloneOptional(manifest);
          response_.results.push_back(std::move(result));
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
      // TODO(thatguy): Return an error string.
      FXL_LOG(WARNING) << "A null-named module parameter is not allowed "
                       << "when using fuchsia::modular::Intent.action.";
      return;
    }

    constraint_futs_.push_back(
        GetTypesFromIntentParameter(requesting_module_path_.Clone(), param.data)
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
    const fuchsia::modular::IntentParameterData& input) {
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
      fut->Complete(GetTypesFromJson(input.json()));
      break;
    }
    case fuchsia::modular::IntentParameterData::Tag::kLinkName: {
      auto did_get_lp = Future<fuchsia::modular::LinkPathPtr>::Create(
          "AddModCommandRunner::GetTypesFromIntentParameter.did_get_lp");
      operations_.Add(new GetLinkPathForParameterNameCall(
          story_storage_, module_path.Clone(), input.link_name(),
          did_get_lp->Completer()));
      did_get_lp->Then([this, fut](fuchsia::modular::LinkPathPtr lp) {
        // TODO(miguelfrde): handle case when no LinkPath is returned.
        // Probably error case should come from the operation call above.
        GetTypesFromLink(std::move(lp), fut->Completer());
      });
      break;
    }
    case fuchsia::modular::IntentParameterData::Tag::kLinkPath: {
      LinkPathPtr lp = CloneOptional(input.link_path());
      GetTypesFromLink(std::move(lp), fut->Completer());
      break;
    }
    case fuchsia::modular::IntentParameterData::Tag::Invalid: {
      break;
    }
  }
  return fut;
}

std::vector<std::string> FindModulesCall::GetTypesFromJson(
    const fidl::StringPtr& input) {
  std::vector<std::string> types;
  if (!ExtractEntityTypesFromJson(input, &types)) {
    FXL_LOG(WARNING) << "Mal-formed JSON in parameter: " << input;
    return {};
  } else {
    return types;
  }
}

void FindModulesCall::GetTypesFromLink(
    fuchsia::modular::LinkPathPtr link_path,
    std::function<void(std::vector<std::string>)> done) {
  story_storage_->GetLinkValue(*link_path)
      ->Then([this, done = std::move(done)](StoryStorage::Status status,
                                            fidl::StringPtr v) {
        // TODO(miguelfrde): fail if wrong story status.
        done(GetTypesFromJson(v));
      });
}

}  // namespace modular
