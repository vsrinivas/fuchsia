// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SESSIONMGR_PUPPET_MASTER_COMMAND_RUNNERS_FOCUS_MOD_COMMAND_RUNNER_H_
#define PERIDOT_BIN_SESSIONMGR_PUPPET_MASTER_COMMAND_RUNNERS_FOCUS_MOD_COMMAND_RUNNER_H_

#include <fuchsia/modular/cpp/fidl.h>

#include "peridot/bin/sessionmgr/puppet_master/command_runners/command_runner.h"

namespace modular {

class FocusModCommandRunner : public CommandRunner {
 public:
  FocusModCommandRunner(fit::function<void(std::string, std::vector<std::string>)> module_focuser);
  ~FocusModCommandRunner();

  void Execute(fidl::StringPtr story_id, StoryStorage* story_storage,
               fuchsia::modular::StoryCommand command,
               fit::function<void(fuchsia::modular::ExecuteResult)> done) override;

 private:
  fit::function<void(std::string, std::vector<std::string>)> module_focuser_;
};

}  // namespace modular

#endif  // PERIDOT_BIN_SESSIONMGR_PUPPET_MASTER_COMMAND_RUNNERS_FOCUS_MOD_COMMAND_RUNNER_H_
