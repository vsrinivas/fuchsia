// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_TESTING_TEST_STORY_COMMAND_EXECUTOR_H_
#define PERIDOT_LIB_TESTING_TEST_STORY_COMMAND_EXECUTOR_H_

#include <vector>

#include <fuchsia/modular/cpp/fidl.h>

#include "peridot/bin/sessionmgr/puppet_master/story_command_executor.h"

namespace modular {
namespace testing {

class TestStoryCommandExecutor : public StoryCommandExecutor {
 public:
  void SetExecuteReturnResult(fuchsia::modular::ExecuteStatus status,
                              fidl::StringPtr error_message);

  void Reset();

  int execute_count() const { return execute_count_; }
  fidl::StringPtr last_story_id() const { return last_story_id_; }
  const std::vector<fuchsia::modular::StoryCommand>& last_commands() const {
    return last_commands_;
  }

 private:
  // |StoryCommandExecutor|
  void ExecuteCommandsInternal(fidl::StringPtr story_id,
                               std::vector<fuchsia::modular::StoryCommand> commands,
                               fit::function<void(fuchsia::modular::ExecuteResult)> done) override;

  int execute_count_{0};
  fidl::StringPtr last_story_id_;
  std::vector<fuchsia::modular::StoryCommand> last_commands_;
  fuchsia::modular::ExecuteResult result_;
};

}  // namespace testing
}  // namespace modular

#endif  // PERIDOT_LIB_TESTING_TEST_STORY_COMMAND_EXECUTOR_H_
