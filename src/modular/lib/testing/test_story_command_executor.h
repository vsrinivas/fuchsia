// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_TESTING_TEST_STORY_COMMAND_EXECUTOR_H_
#define SRC_MODULAR_LIB_TESTING_TEST_STORY_COMMAND_EXECUTOR_H_

#include <fuchsia/modular/cpp/fidl.h>

#include <vector>

#include "src/modular/bin/sessionmgr/puppet_master/story_command_executor.h"
#include "src/modular/bin/sessionmgr/storage/story_storage.h"

namespace modular_testing {

class TestStoryCommandExecutor : public modular::StoryCommandExecutor {
 public:
  // Optional. If a |StoryStorage| is set, certain executed commands perform limited (as-needed to
  // support existing test cases) updates to the |StoryStorage|. See ExecuteCommandsInternal().
  void SetStoryStorage(std::shared_ptr<modular::StoryStorage> story_storage);

  // Change the default return status and optional error message to be returned from
  // |StoryController|->Execute()
  void SetExecuteReturnResult(fuchsia::modular::ExecuteStatus status,
                              std::optional<std::string> error_message);

  // Reset execute_count to 0, and clear the last_story_id and last_commands vector.
  void Reset();

  int execute_count() const { return execute_count_; }
  std::optional<std::string> last_story_id() const { return last_story_id_; }
  const std::vector<fuchsia::modular::StoryCommand>& last_commands() const {
    return last_commands_;
  }

 private:
  // |StoryCommandExecutor|
  void ExecuteCommandsInternal(std::string story_id,
                               std::vector<fuchsia::modular::StoryCommand> commands,
                               fit::function<void(fuchsia::modular::ExecuteResult)> done) override;

  int execute_count_{0};
  std::optional<std::string> last_story_id_;
  std::vector<fuchsia::modular::StoryCommand> last_commands_;
  fuchsia::modular::ExecuteResult result_;
  std::shared_ptr<modular::StoryStorage> story_storage_;
};

}  // namespace modular_testing

#endif  // SRC_MODULAR_LIB_TESTING_TEST_STORY_COMMAND_EXECUTOR_H_
