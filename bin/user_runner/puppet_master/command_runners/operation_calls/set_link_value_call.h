// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_COMMAND_RUNNERS_OPERATION_CALLS_SET_LINK_VALUE_CALL_H_
#define PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_COMMAND_RUNNERS_OPERATION_CALLS_SET_LINK_VALUE_CALL_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/async/cpp/operation.h>
#include "peridot/bin/user_runner/storage/story_storage.h"

namespace modular {

class SetLinkValueCall : public Operation<fuchsia::modular::ExecuteResult> {
 public:
  SetLinkValueCall(StoryStorage* const story_storage,
                   fuchsia::modular::LinkPath link_path,
                   std::function<void(fidl::StringPtr*)> mutate_fn,
                   ResultCall done);

 private:
  void Run() override;

  fidl::StringPtr story_id_;
  StoryStorage* const story_storage_;
  fuchsia::modular::LinkPath link_path_;
  std::function<void(fidl::StringPtr*)> mutate_fn_;
  fuchsia::modular::ExecuteResult result_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SetLinkValueCall);
};

}  // namespace modular

#endif  // PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_COMMAND_RUNNERS_OPERATION_CALLS_SET_LINK_VALUE_CALL_H_
