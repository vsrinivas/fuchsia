// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "peridot/bin/user_runner/puppet_master/command_runners/operation_calls/add_mod_call.h"

#include <lib/entity/cpp/json.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/fxl/logging.h>

#include "peridot/bin/user_runner/puppet_master/command_runners/operation_calls/find_modules_call.h"
#include "peridot/bin/user_runner/puppet_master/command_runners/operation_calls/get_link_path_for_parameter_name_call.h"
#include "peridot/bin/user_runner/puppet_master/command_runners/operation_calls/get_types_from_entity_call.h"
#include "peridot/bin/user_runner/puppet_master/command_runners/operation_calls/initialize_chain_call.h"
#include "peridot/lib/fidl/clone.h"

namespace modular {

AddModCall::AddModCall(StoryStorage* const story_storage,
                       fuchsia::modular::ModuleResolver* const module_resolver,
                       fuchsia::modular::EntityResolver* const entity_resolver,
                       fidl::VectorPtr<fidl::StringPtr> mod_name,
                       fuchsia::modular::Intent intent,
                       fuchsia::modular::SurfaceRelationPtr surface_relation,
                       fidl::VectorPtr<fidl::StringPtr> surface_parent_mod_name,
                       fuchsia::modular::ModuleSource module_source,
                       ResultCall done)
    : Operation("AddModCommandRunner::AddModCall", std::move(done)),
      story_storage_(story_storage),
      module_resolver_(module_resolver),
      entity_resolver_(entity_resolver),
      mod_name_(std::move(mod_name)),
      intent_(std::move(intent)),
      surface_relation_(std::move(surface_relation)),
      surface_parent_mod_name_(std::move(surface_parent_mod_name)),
      module_source_(module_source) {}

// Start by finding the module through module resolver
void AddModCall::Run() {
  FlowToken flow{this, &result_, &module_data_};
  // Success status by default, we'll update it if we find an error state.
  result_.status = fuchsia::modular::ExecuteStatus::OK;

  operation_queue_.Add(new FindModulesCall(
      story_storage_, module_resolver_, entity_resolver_,
      CloneOptional(intent_), surface_parent_mod_name_.Clone(),
      [this, flow](fuchsia::modular::ExecuteResult result,
                   fuchsia::modular::FindModulesResponse response) {
        if (result.status != fuchsia::modular::ExecuteStatus::OK) {
          result_ = std::move(result);
          return;
          // Operation finishes since |flow| goes out of scope.
        }
        if (response.results->empty()) {
          result_.status = fuchsia::modular::ExecuteStatus::NO_MODULES_FOUND;
          result_.error_message = "Resolution of intent gave zero results.";
          return;
          // Operation finishes since |flow| goes out of scope.
        }
        resolver_response_ = std::move(response);
        CreateLinks(flow);
      }));
}

// Create module parameters info and create links.
void AddModCall::CreateLinks(FlowToken flow) {
  CreateModuleParameterMapInfo(flow)->Then([this, flow] {
    if (result_.status != fuchsia::modular::ExecuteStatus::OK) {
      // Early finish since we found an error state.
      return;
    }
    auto full_module_path = surface_parent_mod_name_.Clone();
    full_module_path->insert(full_module_path->end(), mod_name_->begin(),
                             mod_name_->end());
    operation_queue_.Add(new InitializeChainCall(
        story_storage_, std::move(full_module_path), std::move(parameter_info_),
        [this, flow](fuchsia::modular::ExecuteResult result,
                     fuchsia::modular::ModuleParameterMapPtr map) {
          if (result.status != fuchsia::modular::ExecuteStatus::OK) {
            result_ = std::move(result);
            return;
            // Operation finishes since |flow| goes out of scope.
          }
          WriteModuleData(flow, std::move(map));
        }));
  });
}

// Write module data
void AddModCall::WriteModuleData(FlowToken flow,
                                 fuchsia::modular::ModuleParameterMapPtr map) {
  const auto& module_result = resolver_response_.results->at(0);

  fidl::Clone(*map, &module_data_.parameter_map);
  module_data_.module_url = module_result.module_id;
  module_data_.module_path = surface_parent_mod_name_.Clone();
  module_data_.module_path->insert(module_data_.module_path->end(),
                                   mod_name_->begin(), mod_name_->end());
  module_data_.module_source = module_source_;
  module_data_.module_stopped = false;
  fidl::Clone(surface_relation_, &module_data_.surface_relation);
  module_data_.intent =
      std::make_unique<fuchsia::modular::Intent>(std::move(intent_));
  fidl::Clone(module_result.manifest, &module_data_.module_manifest);

  // Operation stays alive until flow goes out of scope.
  fuchsia::modular::ModuleData module_data_out;
  module_data_.Clone(&module_data_out);
  story_storage_->WriteModuleData(std::move(module_data_out))
      ->Then([this, flow] {});
}

FuturePtr<> AddModCall::CreateModuleParameterMapInfo(FlowToken flow) {
  parameter_info_ = fuchsia::modular::CreateModuleParameterMapInfo::New();

  std::vector<FuturePtr<fuchsia::modular::CreateModuleParameterMapEntryPtr>>
      did_get_entries;
  did_get_entries.reserve(intent_.parameters->size());

  for (auto& param : *intent_.parameters) {
    auto entry = fuchsia::modular::CreateModuleParameterMapEntry::New();
    switch (param.data.Which()) {
      case fuchsia::modular::IntentParameterData::Tag::kEntityReference: {
        fuchsia::modular::CreateLinkInfo create_link;
        fsl::SizedVmo vmo;
        FXL_CHECK(fsl::VmoFromString(
            EntityReferenceToJson(param.data.entity_reference()), &vmo));
        create_link.initial_data = std::move(vmo).ToTransport();
        entry->key = param.name;
        entry->value.set_create_link(std::move(create_link));
        break;
      }
      case fuchsia::modular::IntentParameterData::Tag::kEntityType: {
        // Create a link, but don't populate it. This is useful in the event
        // that the link is used as an 'output' link. Setting a valid JSON
        // value for null in the vmo.
        fsl::SizedVmo vmo;
        FXL_CHECK(fsl::VmoFromString("null", &vmo));
        fuchsia::modular::CreateLinkInfo create_link;
        create_link.initial_data = std::move(vmo).ToTransport();
        entry->key = param.name;
        entry->value.set_create_link(std::move(create_link));
        break;
      }
      case fuchsia::modular::IntentParameterData::Tag::kJson: {
        fuchsia::modular::CreateLinkInfo create_link;
        param.data.json().Clone(&create_link.initial_data);
        entry->key = param.name;
        entry->value.set_create_link(std::move(create_link));
        break;
      }
      case fuchsia::modular::IntentParameterData::Tag::kLinkName: {
        auto did_get_lp = Future<fuchsia::modular::LinkPathPtr>::Create(
            "AddModCommandRunner::FindModulesCall::did_get_link");
        // TODO(miguelfrde): get rid of using surface_parent_mod_name this
        // way. Maybe we should just return an INVALID status here since using
        // this parameter in a StoryCommand doesn't make much sense.
        operations_.Add(new GetLinkPathForParameterNameCall(
            story_storage_, surface_parent_mod_name_.Clone(),
            param.data.link_name(), did_get_lp->Completer()));
        did_get_entries.emplace_back(did_get_lp->Map(
            [param_name = param.name](fuchsia::modular::LinkPathPtr link_path) {
              auto entry =
                  fuchsia::modular::CreateModuleParameterMapEntry::New();
              entry->key = param_name;
              entry->value.set_link_path(std::move(*link_path));
              return entry;
            }));
        continue;
      }
      case fuchsia::modular::IntentParameterData::Tag::kLinkPath: {
        fuchsia::modular::LinkPath lp;
        param.data.link_path().Clone(&lp);
        entry->key = param.name;
        entry->value.set_link_path(std::move(lp));
        break;
      }
      case fuchsia::modular::IntentParameterData::Tag::Invalid: {
        std::ostringstream stream;
        stream << "Invalid data for parameter with name: " << param.name;
        result_.error_message = stream.str();
        result_.status = fuchsia::modular::ExecuteStatus::INVALID_COMMAND;
        return Future<>::CreateCompleted(
            "AddModCommandRunner::FindModulesCall.invalid_parameter");
      }
    }

    auto did_create_entry =
        Future<fuchsia::modular::CreateModuleParameterMapEntryPtr>::
            CreateCompleted(
                "AddModCommandRunner::FindModulesCall.did_create_entry",
                std::move(entry));
    did_get_entries.emplace_back(did_create_entry);
  }

  return Wait("AddModCommandRunner::FindModulesCall::Wait", did_get_entries)
      ->Then([this, flow](
                 std::vector<fuchsia::modular::CreateModuleParameterMapEntryPtr>
                     entries) {
        parameter_info_ = fuchsia::modular::CreateModuleParameterMapInfo::New();
        for (auto& entry : entries) {
          parameter_info_->property_info.push_back(std::move(*entry.get()));
        }
      });
}

}  // namespace modular
