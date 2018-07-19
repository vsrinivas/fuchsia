// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/puppet_master/command_runners/update_mod_command_runner.h"

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/internal/cpp/fidl.h>
#include <lib/async/cpp/future.h>
#include <lib/entity/cpp/json.h>

namespace modular {

namespace {

class UpdateModCall : public Operation<fuchsia::modular::ExecuteResult> {
 public:
  UpdateModCall(SessionStorage* const session_storage, fidl::StringPtr story_id,
                fuchsia::modular::UpdateMod command, ResultCall done)
      : Operation("UpdateModCommandRunner::UpdateModCall", std::move(done)),
        session_storage_(session_storage),
        story_id_(std::move(story_id)),
        command_(std::move(command)) {}

 private:
  // Start by fetching the story storage.
  void Run() override {
    FlowToken flow{this, &result_};
    result_.story_id = story_id_;
    session_storage_->GetStoryStorage(story_id_)->Then(
        [this, flow](std::unique_ptr<StoryStorage> story_storage) {
          if (!story_storage) {
            result_.status = fuchsia::modular::ExecuteStatus::INVALID_STORY_ID;
            result_.error_message = "No StoryStorage for given story.";
            Done(std::move(result_));
            return;
          }
          story_storage_ = std::move(story_storage);
          Cont(flow);
        });
  }

  // Once we have the story storage, get the module data to know what links to
  // update.
  void Cont(FlowToken flow) {
    story_storage_->ReadModuleData(command_.mod_name)
        ->Then([this, flow](fuchsia::modular::ModuleDataPtr module_data) {
          if (!module_data) {
            result_.status = fuchsia::modular::ExecuteStatus::INVALID_COMMAND;
            result_.error_message = "No module data";
            Done(std::move(result_));
            return;
          }
          Cont2(flow, std::move(module_data));
        });
  }

  // Once we have the module data, use it to know what links to update and
  // update them if their names match the given parameters.
  void Cont2(FlowToken flow, fuchsia::modular::ModuleDataPtr module_data) {
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
              result.story_id = std::move(result_.story_id);
              Done(std::move(result));
              return;
            }
          }
          result_.status = fuchsia::modular::ExecuteStatus::OK;
          Done(std::move(result_));
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
    return story_storage_
        ->UpdateLinkValue(path,
                          [new_value = new_value](fidl::StringPtr* value) {
                            *value = new_value;
                          },
                          this /* context */)
        ->Map([](StoryStorage::Status status) {
          fuchsia::modular::ExecuteResult result;
          if (status == StoryStorage::Status::OK) {
            result.status = fuchsia::modular::ExecuteStatus::OK;
          } else {
            result.status = fuchsia::modular::ExecuteStatus::INVALID_COMMAND;
            std::stringstream stream;
            stream << "StoryStorage error status:" << (uint32_t)status;
            result.error_message = stream.str();
          }
          return result;
        });
  }

  SessionStorage* const session_storage_;
  fidl::StringPtr story_id_;
  fuchsia::modular::UpdateMod command_;
  std::unique_ptr<StoryStorage> story_storage_;
  fuchsia::modular::ExecuteResult result_;

  FXL_DISALLOW_COPY_AND_ASSIGN(UpdateModCall);
};

}  // namespace

UpdateModCommandRunner::UpdateModCommandRunner(
    SessionStorage* const session_storage)
    : CommandRunner(session_storage) {}

UpdateModCommandRunner::~UpdateModCommandRunner() = default;

void UpdateModCommandRunner::Execute(
    fidl::StringPtr story_id, fuchsia::modular::StoryCommand command,
    std::function<void(fuchsia::modular::ExecuteResult)> done) {
  FXL_CHECK(command.is_update_mod());

  operation_queue_.Add(new UpdateModCall(session_storage_, std::move(story_id),
                                         std::move(command.update_mod()),
                                         std::move(done)));
}

}  // namespace modular
