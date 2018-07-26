// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_COMMAND_RUNNERS_COMMAND_RUNNER_H_
#define PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_COMMAND_RUNNERS_COMMAND_RUNNER_H_

#include <fuchsia/modular/cpp/fidl.h>

#include "peridot/bin/user_runner/storage/session_storage.h"

namespace modular {

class CommandRunner {
 public:
  CommandRunner();
  virtual ~CommandRunner();

  virtual void Execute(
      fidl::StringPtr story_id, StoryStorage* story_storage,
      fuchsia::modular::StoryCommand command,
      std::function<void(fuchsia::modular::ExecuteResult)> done) = 0;
};

}  // namespace modular

#endif  // PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_COMMAND_RUNNERS_COMMAND_RUNNER_H_
