// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/puppet_master/command_runners/remove_mod_command_runner.h"

#include "src/lib/syslog/cpp/logger.h"

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

    // Prefer |mod_name_transitional| over |mod_name|
    std::vector<std::string> mod_name{};
    if (command_.mod_name_transitional.has_value()) {
      mod_name.push_back(*command_.mod_name_transitional);
    } else {
      mod_name = command_.mod_name;
    }

    // Set the module data stopped to true, this should notify story
    // controller and perform module teardown.
    story_storage_
        ->UpdateModuleData(mod_name,
                           [this, flow](fuchsia::modular::ModuleDataPtr* module_data) {
                             if (!(*module_data)) {
                               result_.status = fuchsia::modular::ExecuteStatus::INVALID_MOD;
                               result_.error_message = "No module data for given name.";
                               return;
                             }
                             (*module_data)->set_module_deleted(true);
                             result_.status = fuchsia::modular::ExecuteStatus::OK;
                           })
        ->Then([flow] {});
  }

  StoryStorage* const story_storage_;
  fidl::StringPtr story_id_;
  fuchsia::modular::RemoveMod command_;
  fuchsia::modular::ExecuteResult result_;
};

}  // namespace

RemoveModCommandRunner::RemoveModCommandRunner() = default;
RemoveModCommandRunner::~RemoveModCommandRunner() = default;

void RemoveModCommandRunner::Execute(fidl::StringPtr story_id, StoryStorage* const story_storage,
                                     fuchsia::modular::StoryCommand command,
                                     fit::function<void(fuchsia::modular::ExecuteResult)> done) {
  FX_CHECK(command.is_remove_mod());

  operation_queue_.Add(std::make_unique<RemoveModCall>(
      story_storage, std::move(story_id), std::move(command.remove_mod()), std::move(done)));
}

}  // namespace modular
