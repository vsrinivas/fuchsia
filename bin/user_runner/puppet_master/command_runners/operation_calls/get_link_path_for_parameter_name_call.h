// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_COMMAND_RUNNERS_OPERATION_CALLS_GET_LINK_PATH_FOR_PARAMETER_NAME_CALL_H_
#define PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_COMMAND_RUNNERS_OPERATION_CALLS_GET_LINK_PATH_FOR_PARAMETER_NAME_CALL_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/async/cpp/operation.h>
#include "peridot/bin/user_runner/storage/story_storage.h"

namespace modular {

class GetLinkPathForParameterNameCall
    : public Operation<fuchsia::modular::LinkPathPtr> {
 public:
  GetLinkPathForParameterNameCall(StoryStorage* const story_storage,
                                  fidl::VectorPtr<fidl::StringPtr> module_name,
                                  fidl::StringPtr link_name,
                                  ResultCall result_call);

 private:
  void Run() override;

  StoryStorage* const story_storage_;  // Not owned.
  fidl::VectorPtr<fidl::StringPtr> module_name_;
  fidl::StringPtr link_name_;
  fuchsia::modular::LinkPathPtr link_path_;
};

}  // namespace modular

#endif  // PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_COMMAND_RUNNERS_OPERATION_CALLS_GET_LINK_PATH_FOR_PARAMETER_NAME_CALL_H_
