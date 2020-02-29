// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_SESSIONMGR_PUPPET_MASTER_COMMAND_RUNNERS_ADD_MOD_COMMAND_RUNNER_H_
#define SRC_MODULAR_BIN_SESSIONMGR_PUPPET_MASTER_COMMAND_RUNNERS_ADD_MOD_COMMAND_RUNNER_H_

#include <fuchsia/modular/cpp/fidl.h>

#include "src/modular/bin/sessionmgr/puppet_master/command_runners/command_runner.h"
#include "src/modular/lib/async/cpp/operation.h"

namespace modular {

class AddModCommandRunner : public CommandRunner {
 public:
  AddModCommandRunner();
  ~AddModCommandRunner() override;

  void Execute(fidl::StringPtr story_id, StoryStorage* story_storage,
               fuchsia::modular::StoryCommand command,
               fit::function<void(fuchsia::modular::ExecuteResult)> done) override;

 private:
  OperationQueue operation_queue_;
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_SESSIONMGR_PUPPET_MASTER_COMMAND_RUNNERS_ADD_MOD_COMMAND_RUNNER_H_
