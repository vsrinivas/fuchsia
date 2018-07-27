// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/puppet_master/command_runners/update_mod_command_runner.h"

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/internal/cpp/fidl.h>
#include <lib/async/cpp/future.h>
#include <lib/entity/cpp/json.h>

#include "peridot/bin/user_runner/puppet_master/command_runners/operation_calls/set_link_value_call.h"

namespace modular {

namespace {

class UpdateModCall : public Operation<fuchsia::modular::ExecuteResult> {
 public:
  UpdateModCall(StoryStorage* const story_storage,
                fuchsia::modular::UpdateMod command, ResultCall done)
      : Operation("UpdateModCommandRunner::UpdateModCall", std::move(done)),
        story_storage_(story_storage),
        command_(std::move(command)) {}

 private:
  void Run() override {
    FlowToken flow{this, &result_};
    story_storage_->ReadModuleData(command_.mod_name)
        ->Then([this, flow](fuchsia::modular::ModuleDataPtr module_data) {
          if (!module_data) {
            result_.status = fuchsia::modular::ExecuteStatus::INVALID_COMMAND;
            result_.error_message = "No module data";
            Done(std::move(result_));
            return;
          }
          Cont1(flow, std::move(module_data));
        });
  }

  // Once we have the module data, use it to know what links to update and
  // update them if their names match the given parameters.
  void Cont1(FlowToken flow, fuchsia::modular::ModuleDataPtr module_data) {
    std::vector<modular::FuturePtr<fuchsia::modular::ExecuteResult>>
        did_update_links;
    for (const auto& parameter : *command_.parameters) {
      for (const auto& entry : *module_data->parameter_map.entries) {
        if (parameter.name != entry.name) {
          continue;
        }
        did_update_links.push_back(
            UpdateLinkValue(entry.link_path, parameter.data));
      }
    }
    Wait("UpdateModCommandRunner.UpdateMod.Wait", did_update_links)
        ->Then([this, flow](
                   std::vector<fuchsia::modular::ExecuteResult> result_values) {
          for (auto& result : result_values) {
            if (result.status != fuchsia::modular::ExecuteStatus::OK) {
              Done(std::move(result));
              return;
            }
          }
          result_.status = fuchsia::modular::ExecuteStatus::OK;
        });
  }

  FuturePtr<fuchsia::modular::ExecuteResult> UpdateLinkValue(
      const fuchsia::modular::LinkPath& path,
      const fuchsia::modular::IntentParameterData& data) {
    std::string new_value;
    switch (data.Which()) {
      case fuchsia::modular::IntentParameterData::Tag::kEntityReference:
        new_value = EntityReferenceToJson(data.entity_reference());
        break;
      case fuchsia::modular::IntentParameterData::Tag::kJson:
        new_value = data.json();
        break;
      case fuchsia::modular::IntentParameterData::Tag::kEntityType:
      case fuchsia::modular::IntentParameterData::Tag::kLinkName:
      case fuchsia::modular::IntentParameterData::Tag::kLinkPath:
      case fuchsia::modular::IntentParameterData::Tag::Invalid:
        fuchsia::modular::ExecuteResult result;
        result.status = fuchsia::modular::ExecuteStatus::INVALID_COMMAND;
        std::stringstream stream;
        stream << "Unsupported IntentParameterData type:"
               << (uint32_t)data.Which();
        auto ret = Future<fuchsia::modular::ExecuteResult>::CreateCompleted(
            "UpdateModCommandRunner.UpdateLinkValue.ret", std::move(result));
        return ret;
    }
    auto fut = Future<fuchsia::modular::ExecuteResult>::Create(
        "UpdateModCommandRunner.UpdateLinkValue.fut");
    fuchsia::modular::LinkPath out_path;
    path.Clone(&out_path);
    operations_.Add(new SetLinkValueCall(
        story_storage_, std::move(out_path),
        [new_value](fidl::StringPtr* value) { *value = new_value; },
        fut->Completer()));
    return fut;
  }

  StoryStorage* const story_storage_;
  fuchsia::modular::UpdateMod command_;
  fuchsia::modular::ExecuteResult result_;
  OperationCollection operations_;

  FXL_DISALLOW_COPY_AND_ASSIGN(UpdateModCall);
};

}  // namespace

UpdateModCommandRunner::UpdateModCommandRunner() = default;
UpdateModCommandRunner::~UpdateModCommandRunner() = default;

void UpdateModCommandRunner::Execute(
    fidl::StringPtr story_id, StoryStorage* const story_storage,
    fuchsia::modular::StoryCommand command,
    std::function<void(fuchsia::modular::ExecuteResult)> done) {
  FXL_CHECK(command.is_update_mod());

  operation_queue_.Add(new UpdateModCall(
      story_storage, std::move(command.update_mod()), std::move(done)));
}

}  // namespace modular
