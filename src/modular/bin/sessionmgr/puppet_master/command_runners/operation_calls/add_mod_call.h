// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_SESSIONMGR_PUPPET_MASTER_COMMAND_RUNNERS_OPERATION_CALLS_ADD_MOD_CALL_H_
#define SRC_MODULAR_BIN_SESSIONMGR_PUPPET_MASTER_COMMAND_RUNNERS_OPERATION_CALLS_ADD_MOD_CALL_H_

#include <fuchsia/modular/cpp/fidl.h>

#include "src/modular/bin/sessionmgr/storage/story_storage.h"
#include "src/modular/lib/async/cpp/operation.h"

namespace modular {

// This struct contains common parameters needed to add a module to a story,
// useful as a single place to add more parameters that need shuffling around.
// See story_command.fidl and module_data.fidl for a detailed description of
// what these parameters mean.
struct AddModParams {
  // This parent module's module path. If empty, this mod has no parent module.
  std::vector<std::string> parent_mod_path;
  // Module name given to this module path (parent_mod_path + mod_name is this
  // module's module path).
  std::string mod_name;

  // True if this is an embedded mod (as opposed to being arranged by the story
  // shell), in which case this mod's view will be embedded by its parent mod
  // (represented by parent_mod_path).
  bool is_embedded;

  // See |fuchsia::modular::ModuleData| (module_data.fidl) for a detailed
  // description of what these parameters mean.
  fuchsia::modular::Intent intent;
  fuchsia::modular::SurfaceRelationPtr surface_relation;
  fuchsia::modular::ModuleSource module_source;
};

void AddAddModOperation(
    OperationContainer* container, StoryStorage* story_storage, AddModParams add_mod_params,
    fit::function<void(fuchsia::modular::ExecuteResult, fuchsia::modular::ModuleData)> done);

}  // namespace modular

#endif  // SRC_MODULAR_BIN_SESSIONMGR_PUPPET_MASTER_COMMAND_RUNNERS_OPERATION_CALLS_ADD_MOD_CALL_H_
