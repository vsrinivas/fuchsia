// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_STORY_COMMAND_EXECUTOR_H_
#define PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_STORY_COMMAND_EXECUTOR_H_

#include <functional>
#include <vector>

#include <fuchsia/modular/cpp/fidl.h>

namespace modular {

class StoryCommandExecutor {
 public:
  virtual ~StoryCommandExecutor();

  // Executes |commands| on story identified by |story_id| and calls |done| when
  // complete. |story_id| is always non-null and refers to an existing Story.
  //
  // If an error occurs, fuchsia::modular::ExecuteResult.status will be set to
  // indicate the type of error, and a helpful error message must also be
  // provided in fuchsia::modular::ExecuteResult.error_message.
  //
  // On success fuchsia::modular::ExecuteResult.status will be set to
  // fuchsia::modular::ExecuteStatus.OK.
  virtual void ExecuteCommands(
      fidl::StringPtr story_id,
      std::vector<fuchsia::modular::StoryCommand> commands,
      std::function<void(fuchsia::modular::ExecuteResult)> done) = 0;
};

}  // namespace modular

#endif  // PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_STORY_COMMAND_EXECUTOR_H_
