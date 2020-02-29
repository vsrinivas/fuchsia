// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_SESSIONMGR_PUPPET_MASTER_MAKE_PRODUCTION_IMPL_H_
#define SRC_MODULAR_BIN_SESSIONMGR_PUPPET_MASTER_MAKE_PRODUCTION_IMPL_H_

#include <memory>

#include "src/modular/bin/sessionmgr/puppet_master/dispatch_story_command_executor.h"

namespace modular {

class StoryCommandExecutor;

// Returns a StoryCommandExecutor suitable for use in production.
std::unique_ptr<StoryCommandExecutor> MakeProductionStoryCommandExecutor(
    SessionStorage* session_storage, fuchsia::modular::FocusProviderPtr focus_provider,
    fit::function<void(std::string, std::vector<std::string>)> module_focuser);

}  // namespace modular

#endif  // SRC_MODULAR_BIN_SESSIONMGR_PUPPET_MASTER_MAKE_PRODUCTION_IMPL_H_
