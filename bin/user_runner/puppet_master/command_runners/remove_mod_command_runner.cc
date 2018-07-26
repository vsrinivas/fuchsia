// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/puppet_master/command_runners/remove_mod_command_runner.h"

#include <lib/fxl/logging.h>

namespace modular {

namespace {

class RemoveModCall : public Operation<fuchsia::modular::ExecuteResult> {
 public:
  RemoveModCall(StoryStorage* const story_storage, fidl::StringPtr story_id,
                fuchsia::modular::RemoveMod command, ResultCall done)
      : Operation("RemoveModCommandRunner::RemoveModCall", std::move(done)),
        story_storage_(story_storage),
        story_id_(std::move(story_id)),
        command_(std::move(command)) {}

 private:
  void Run() override {
    FlowToken flow{this, &result_};
    // Set the module data stopped to true, this should notify story
    // controller and perform module teardown.
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
        ->Then([this, flow] {});
  }

  StoryStorage* const story_storage_;
  fidl::StringPtr story_id_;
  fuchsia::modular::RemoveMod command_;
  fuchsia::modular::ExecuteResult result_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RemoveModCall);
};

}  // namespace

RemoveModCommandRunner::RemoveModCommandRunner() = default;
RemoveModCommandRunner::~RemoveModCommandRunner() = default;

void RemoveModCommandRunner::Execute(
    fidl::StringPtr story_id, StoryStorage* const story_storage,
    fuchsia::modular::StoryCommand command,
    std::function<void(fuchsia::modular::ExecuteResult)> done) {
  FXL_CHECK(command.is_remove_mod());

  operation_queue_.Add(new RemoveModCall(story_storage, std::move(story_id),
                                         std::move(command.remove_mod()),
                                         std::move(done)));
}

}  // namespace modular
