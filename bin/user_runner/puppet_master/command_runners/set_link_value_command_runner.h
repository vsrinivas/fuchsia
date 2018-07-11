// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_COMMAND_RUNNERS_SET_LINK_VALUE_COMMAND_RUNNER_H_
#define PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_COMMAND_RUNNERS_SET_LINK_VALUE_COMMAND_RUNNER_H_

#include <fuchsia/modular/cpp/fidl.h>

#include "peridot/bin/user_runner/puppet_master/command_runners/command_runner.h"
#include "peridot/bin/user_runner/storage/story_storage.h"

namespace modular {

class SetLinkValueCommandRunner : public CommandRunner {
 public:
  SetLinkValueCommandRunner(SessionStorage* const session_storage);
  ~SetLinkValueCommandRunner();

  void Execute(
      fidl::StringPtr story_id, fuchsia::modular::StoryCommand command,
      std::function<void(fuchsia::modular::ExecuteResult)> done) override;

 private:
  // Returns a future that will update the link value at |link_path| with
  // |value| using the given |story_storage|. |story_id| is the id of the story
  // of the given |story_storage|.
  // The returned |ExecuteResult| indicates whether or not the update was
  // successful and if not, why it wasn't.
  FuturePtr<fuchsia::modular::ExecuteResult> UpdateLinkValue(
      std::unique_ptr<StoryStorage> story_storage,
      const fidl::StringPtr& story_id, const fuchsia::modular::LinkPath& path,
      std::string value);
};

}  // namespace modular

#endif  // PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_COMMAND_RUNNERS_SET_LINK_VALUE_COMMAND_RUNNER_H_
