// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SESSIONMGR_PUPPET_MASTER_COMMAND_RUNNERS_OPERATION_CALLS_ADD_MOD_CALL_H_
#define PERIDOT_BIN_SESSIONMGR_PUPPET_MASTER_COMMAND_RUNNERS_OPERATION_CALLS_ADD_MOD_CALL_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/async/cpp/operation.h>

#include "peridot/bin/sessionmgr/storage/story_storage.h"

namespace modular {

void AddAddModOperation(
    OperationContainer* container, StoryStorage* story_storage,
    fuchsia::modular::ModuleResolver* module_resolver,
    fuchsia::modular::EntityResolver* entity_resolver,
    std::vector<std::string> mod_name, fuchsia::modular::Intent intent,
    fuchsia::modular::SurfaceRelationPtr surface_relation,
    std::vector<std::string> surface_parent_mod_name,
    fuchsia::modular::ModuleSource module_source,
    std::function<void(fuchsia::modular::ExecuteResult,
                       fuchsia::modular::ModuleData)>
        done);

}  // namespace modular

#endif  // PERIDOT_BIN_SESSIONMGR_PUPPET_MASTER_COMMAND_RUNNERS_OPERATION_CALLS_ADD_MOD_CALL_H_
