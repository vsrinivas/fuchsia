// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/puppet_master/command_runners/remove_mod_command_runner.h"

#include <lib/fxl/logging.h>

namespace modular {

namespace {

class RemoveModCall : public Operation<fuchsia::modular::ExecuteResult> {
 public:
  RemoveModCall(SessionStorage* const session_storage, fidl::StringPtr story_id,
                fuchsia::modular::RemoveMod command, ResultCall done)
      : Operation("RemoveModCommandRunner::RemoveModCall", std::move(done)),
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

  // Now, set the module data stopped to true, this should notify story
  // controller and perform module teardown.
  void Cont(FlowToken flow) {
    story_storage_
        ->UpdateModuleData(
            command_.mod_name,
            [this, flow](fuchsia::modular::ModuleDataPtr* module_data) {
              if (!(*module_data)) {
                result_.status = fuchsia::modular::ExecuteStatus::INVALID_MOD;
                result_.error_message = "No module data for given name.";
                return;
              }
              (*module_data)->module_stopped = true;
              result_.status = fuchsia::modular::ExecuteStatus::OK;
            })
        ->Then([this, flow] { Done(std::move(result_)); });
  }

  SessionStorage* const session_storage_;
  fidl::StringPtr story_id_;
  fuchsia::modular::RemoveMod command_;
  std::unique_ptr<StoryStorage> story_storage_;
  fuchsia::modular::ExecuteResult result_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RemoveModCall);
};

}  // namespace

RemoveModCommandRunner::RemoveModCommandRunner(
    SessionStorage* const session_storage)
    : CommandRunner(session_storage) {}

RemoveModCommandRunner::~RemoveModCommandRunner() = default;

void RemoveModCommandRunner::Execute(
    fidl::StringPtr story_id, fuchsia::modular::StoryCommand command,
    std::function<void(fuchsia::modular::ExecuteResult)> done) {
  FXL_CHECK(command.is_remove_mod());

  operation_queue_.Add(new RemoveModCall(session_storage_, std::move(story_id),
                                         std::move(command.remove_mod()),
                                         std::move(done)));
}

}  // namespace modular
