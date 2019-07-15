// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SESSIONMGR_PUPPET_MASTER_COMMAND_RUNNERS_OPERATION_CALLS_FIND_MODULES_CALL_H_
#define PERIDOT_BIN_SESSIONMGR_PUPPET_MASTER_COMMAND_RUNNERS_OPERATION_CALLS_FIND_MODULES_CALL_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/async/cpp/operation.h>

namespace modular {

void AddFindModulesOperation(
    OperationContainer* operation_container, fuchsia::modular::ModuleResolver* module_resolver,
    fuchsia::modular::EntityResolver* entity_resolver, fuchsia::modular::IntentPtr intent,
    std::vector<std::string> requesting_module_path,
    fit::function<void(fuchsia::modular::ExecuteResult, fuchsia::modular::FindModulesResponse)>
        result_call);

}  // namespace modular

#endif  // PERIDOT_BIN_SESSIONMGR_PUPPET_MASTER_COMMAND_RUNNERS_OPERATION_CALLS_FIND_MODULES_CALL_H_
