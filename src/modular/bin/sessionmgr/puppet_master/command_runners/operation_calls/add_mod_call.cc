// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/modular/bin/sessionmgr/puppet_master/command_runners/operation_calls/add_mod_call.h"

#include <lib/fidl/cpp/clone.h>

#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/modular/bin/sessionmgr/puppet_master/command_runners/operation_calls/get_types_from_entity_call.h"
#include "src/modular/bin/sessionmgr/puppet_master/command_runners/operation_calls/initialize_chain_call.h"
#include "src/modular/lib/entity/cpp/json.h"
#include "src/modular/lib/fidl/clone.h"

namespace modular {

namespace {

class AddModCall : public Operation<fuchsia::modular::ExecuteResult, fuchsia::modular::ModuleData> {
 public:
  AddModCall(StoryStorage* const story_storage, AddModParams add_mod_params, ResultCall done)
      : Operation("AddModCommandRunner::AddModCall", std::move(done)),
        story_storage_(story_storage),
        add_mod_params_(std::move(add_mod_params)) {}

 private:
  void Run() override {
    FlowToken flow{this, &out_result_, &out_module_data_};

    // Success status by default, it will be update it if an error state is
    // found.
    out_result_.status = fuchsia::modular::ExecuteStatus::OK;

    if (add_mod_params_.intent.action.has_value() && !add_mod_params_.intent.handler.has_value()) {
      out_result_.status = fuchsia::modular::ExecuteStatus::NO_MODULES_FOUND;
      out_result_.error_message = "Module resolution via Intent.action is deprecated.";
      return;
    }

    CreateLinks(flow);
  }

  // Create module parameters info and create links.
  void CreateLinks(FlowToken flow) {
    CreateModuleParameterMapInfo(flow, [this, flow] {
      if (out_result_.status != fuchsia::modular::ExecuteStatus::OK) {
        return;
        // Operation finishes since |flow| goes out of scope.
      }
      auto full_module_path = add_mod_params_.parent_mod_path;
      full_module_path.push_back(add_mod_params_.mod_name);
      AddInitializeChainOperation(&operation_queue_, story_storage_, std::move(full_module_path),
                                  std::move(parameter_info_),
                                  [this, flow](fuchsia::modular::ExecuteResult result,
                                               fuchsia::modular::ModuleParameterMapPtr map) {
                                    if (result.status != fuchsia::modular::ExecuteStatus::OK) {
                                      out_result_ = std::move(result);
                                      return;
                                      // Operation finishes since |flow| goes out of scope.
                                    }
                                    WriteModuleData(flow, std::move(map));
                                  });
    });
  }

  // On success, populates |parameter_info_|. On failure, |out_result_| contains
  // error reason. Calls |done()| on completion in either case.
  void CreateModuleParameterMapInfo(FlowToken flow, fit::function<void()> done) {
    parameter_info_ = fuchsia::modular::CreateModuleParameterMapInfo::New();

    std::vector<FuturePtr<fuchsia::modular::CreateModuleParameterMapEntry>> did_get_entries;
    if (add_mod_params_.intent.parameters.has_value()) {
      did_get_entries.reserve(add_mod_params_.intent.parameters->size());

      for (auto& param : *add_mod_params_.intent.parameters) {
        fuchsia::modular::CreateModuleParameterMapEntry entry;
        entry.key = param.name;

        switch (param.data.Which()) {
          case fuchsia::modular::IntentParameterData::Tag::kEntityReference: {
            fuchsia::modular::CreateLinkInfo create_link;
            fsl::SizedVmo vmo;
            FX_CHECK(
                fsl::VmoFromString(EntityReferenceToJson(param.data.entity_reference()), &vmo));
            create_link.initial_data = std::move(vmo).ToTransport();
            entry.value.set_create_link(std::move(create_link));
            break;
          }
          case fuchsia::modular::IntentParameterData::Tag::kEntityType: {
            // Create a link, but don't populate it. This is useful in the event
            // that the link is used as an 'output' link. Setting a valid JSON
            // value for null in the vmo.
            fsl::SizedVmo vmo;
            FX_CHECK(fsl::VmoFromString("null", &vmo));
            fuchsia::modular::CreateLinkInfo create_link;
            create_link.initial_data = std::move(vmo).ToTransport();
            entry.value.set_create_link(std::move(create_link));
            break;
          }
          case fuchsia::modular::IntentParameterData::Tag::kJson: {
            fuchsia::modular::CreateLinkInfo create_link;
            param.data.json().Clone(&create_link.initial_data);
            entry.value.set_create_link(std::move(create_link));
            break;
          }
          case fuchsia::modular::IntentParameterData::Tag::Invalid: {
            out_result_.status = fuchsia::modular::ExecuteStatus::INVALID_COMMAND;
            out_result_.error_message = fxl::StringPrintf(
                "Invalid data for parameter with name: %s", param.name.value_or("").c_str());
            done();
            return;
          }
        }

        auto did_create_entry =
            Future<fuchsia::modular::CreateModuleParameterMapEntry>::CreateCompleted(
                "AddModCommandRunner::FindModulesCall.did_create_entry", std::move(entry));
        did_get_entries.emplace_back(std::move(did_create_entry));
      }
    }

    Wait("AddModCommandRunner::AddModCall::Wait", did_get_entries)
        ->Then([this, done = std::move(done),
                flow](std::vector<fuchsia::modular::CreateModuleParameterMapEntry> entries) {
          parameter_info_->property_info.emplace(std::move(entries));
          done();
        });
  }

  // Write module data
  void WriteModuleData(FlowToken flow, fuchsia::modular::ModuleParameterMapPtr map) {
    fidl::Clone(*map, out_module_data_.mutable_parameter_map());
    out_module_data_.set_module_url(add_mod_params_.intent.handler.value());
    out_module_data_.set_module_path(add_mod_params_.parent_mod_path);
    out_module_data_.mutable_module_path()->push_back(add_mod_params_.mod_name);
    out_module_data_.set_module_source(add_mod_params_.module_source);
    out_module_data_.set_module_deleted(false);
    if (!add_mod_params_.surface_relation) {
      out_module_data_.clear_surface_relation();
    } else {
      fidl::Clone(*add_mod_params_.surface_relation, out_module_data_.mutable_surface_relation());
    }
    out_module_data_.set_is_embedded(add_mod_params_.is_embedded);
    out_module_data_.set_intent(std::move(add_mod_params_.intent));

    // Operation stays alive until flow goes out of scope.
    fuchsia::modular::ModuleData module_data;
    out_module_data_.Clone(&module_data);
    story_storage_->WriteModuleData(std::move(module_data))->Then([flow] {});
  }

  StoryStorage* const story_storage_;  // Not owned.
  modular::AddModParams add_mod_params_;
  fuchsia::modular::CreateModuleParameterMapInfoPtr parameter_info_;
  fuchsia::modular::ModuleData out_module_data_;
  fuchsia::modular::ExecuteResult out_result_;
  // Used when creating the map info to execute an operation as soon as it
  // arrives.
  OperationCollection operations_;
  // Used to enqueue sub-operations that should be executed sequentially.
  OperationQueue operation_queue_;
};

}  // namespace

void AddAddModOperation(
    OperationContainer* const container, StoryStorage* const story_storage,
    AddModParams add_mod_params,
    fit::function<void(fuchsia::modular::ExecuteResult, fuchsia::modular::ModuleData)> done) {
  container->Add(
      std::make_unique<AddModCall>(story_storage, std::move(add_mod_params), std::move(done)));
}

}  // namespace modular
