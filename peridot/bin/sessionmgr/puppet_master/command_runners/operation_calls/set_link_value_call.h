// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SESSIONMGR_PUPPET_MASTER_COMMAND_RUNNERS_OPERATION_CALLS_SET_LINK_VALUE_CALL_H_
#define PERIDOT_BIN_SESSIONMGR_PUPPET_MASTER_COMMAND_RUNNERS_OPERATION_CALLS_SET_LINK_VALUE_CALL_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/async/cpp/operation.h>

#include "peridot/bin/sessionmgr/storage/story_storage.h"

namespace modular {

void AddSetLinkValueOperation(OperationContainer* const operation_container,
                              StoryStorage* const story_storage,
                              fuchsia::modular::LinkPath link_path,
                              fit::function<void(fidl::StringPtr*)> mutate_fn,
                              fit::function<void(fuchsia::modular::ExecuteResult)> done);

}  // namespace modular

#endif  // PERIDOT_BIN_SESSIONMGR_PUPPET_MASTER_COMMAND_RUNNERS_OPERATION_CALLS_SET_LINK_VALUE_CALL_H_
