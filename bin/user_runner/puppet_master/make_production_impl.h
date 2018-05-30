// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "peridot/bin/user_runner/puppet_master/dispatch_story_command_executor.h"

namespace fuchsia {
namespace modular {

class StoryCommandExecutor;

// Returns a StoryCommandExecutor suitable for use in production.
std::unique_ptr<StoryCommandExecutor> MakeProductionStoryCommandExecutor(
    DispatchStoryCommandExecutor::OperationContainerAccessor factory);

}  // namespace modular
}  // namespace fuchsia
