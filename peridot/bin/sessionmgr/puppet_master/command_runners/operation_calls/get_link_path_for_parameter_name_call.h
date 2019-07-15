// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SESSIONMGR_PUPPET_MASTER_COMMAND_RUNNERS_OPERATION_CALLS_GET_LINK_PATH_FOR_PARAMETER_NAME_CALL_H_
#define PERIDOT_BIN_SESSIONMGR_PUPPET_MASTER_COMMAND_RUNNERS_OPERATION_CALLS_GET_LINK_PATH_FOR_PARAMETER_NAME_CALL_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/async/cpp/operation.h>

#include "peridot/bin/sessionmgr/storage/story_storage.h"

namespace modular {

void AddGetLinkPathForParameterNameOperation(
    OperationContainer* operation_container, StoryStorage* const story_storage,
    std::vector<std::string> module_name, std::string link_name,
    fit::function<void(fuchsia::modular::LinkPathPtr)> result_call);

}  // namespace modular

#endif  // PERIDOT_BIN_SESSIONMGR_PUPPET_MASTER_COMMAND_RUNNERS_OPERATION_CALLS_GET_LINK_PATH_FOR_PARAMETER_NAME_CALL_H_
