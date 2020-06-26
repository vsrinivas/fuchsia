// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/testing/test_story_command_executor.h"

namespace modular_testing {

void TestStoryCommandExecutor::SetStoryStorage(
    std::shared_ptr<modular::StoryStorage> story_storage) {
  story_storage_ = std::move(story_storage);
}

void TestStoryCommandExecutor::SetExecuteReturnResult(fuchsia::modular::ExecuteStatus status,
                                                      std::optional<std::string> error_message) {
  result_.status = status;
  result_.error_message = error_message ? fidl::StringPtr(error_message.value()) : std::nullopt;
}

void TestStoryCommandExecutor::Reset() {
  last_story_id_.reset();
  last_commands_.clear();
  execute_count_ = 0;
}

void TestStoryCommandExecutor::ExecuteCommandsInternal(
    std::string story_id, std::vector<fuchsia::modular::StoryCommand> commands,
    fit::function<void(fuchsia::modular::ExecuteResult)> done) {
  ++execute_count_;

  fuchsia::modular::ExecuteResult result;
  fidl::Clone(result_, &result);
  result.story_id = story_id;

  if (!!story_storage_) {
    for (auto& command : commands) {
      if (command.is_add_mod()) {
        auto& add_mod = command.add_mod();

        fuchsia::modular::ModuleData module_data;
        if (add_mod.mod_name_transitional) {
          module_data.set_module_url(*add_mod.mod_name_transitional);
          module_data.set_module_path({*add_mod.mod_name_transitional});
        } else {
          module_data.set_module_url(add_mod.mod_name.back());
          module_data.set_module_path(add_mod.mod_name);
        }
        module_data.set_module_source(fuchsia::modular::ModuleSource::INTERNAL);
        module_data.set_module_deleted(false);
        // This test currently ignores the following fields:
        //   Intent intent
        //   SurfaceRelation surface_relation

        story_storage_->WriteModuleData(std::move(module_data));

        // This test currently only persists adding mods (assuming there is a story_storage_);
        // other commands are not yet persisted, such as:
        //
        //   else if (command.is_remove_mod())
      }
    }
  }
  last_story_id_ = story_id;
  last_commands_ = std::move(commands);
  done(std::move(result));
}

}  // namespace modular_testing
