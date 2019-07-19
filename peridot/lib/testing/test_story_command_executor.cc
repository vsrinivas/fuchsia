// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "peridot/lib/testing/test_story_command_executor.h"

namespace modular {
namespace testing {

void TestStoryCommandExecutor::SetExecuteReturnResult(fuchsia::modular::ExecuteStatus status,
                                                      fidl::StringPtr error_message) {
  result_.status = status;
  result_.error_message = error_message;
}

void TestStoryCommandExecutor::Reset() {
  last_story_id_.reset();
  last_commands_.clear();
  execute_count_ = 0;
}

void TestStoryCommandExecutor::ExecuteCommandsInternal(
    fidl::StringPtr story_id, std::vector<fuchsia::modular::StoryCommand> commands,
    fit::function<void(fuchsia::modular::ExecuteResult)> done) {
  ++execute_count_;
  last_story_id_ = story_id;
  last_commands_ = std::move(commands);
  fuchsia::modular::ExecuteResult result;
  fidl::Clone(result_, &result);
  result.story_id = story_id;
  done(std::move(result));
}

}  // namespace testing
}  // namespace modular
