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
  // complete. If |story_id| is null, a new story will be created. In either
  // case, fuchsia::modular::ExecuteResult.story_id will be set to the id of the
  // story on which commands were executed. In the case |story_id| is not null,
  // this will be exactly the value of |story_id|.
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
