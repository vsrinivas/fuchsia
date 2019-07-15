// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SESSIONMGR_PUPPET_MASTER_COMMAND_RUNNERS_OPERATION_CALLS_GET_TYPES_FROM_ENTITY_CALL_H_
#define PERIDOT_BIN_SESSIONMGR_PUPPET_MASTER_COMMAND_RUNNERS_OPERATION_CALLS_GET_TYPES_FROM_ENTITY_CALL_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/async/cpp/operation.h>

namespace modular {

void AddGetTypesFromEntityOperation(OperationContainer* operation_container,
                                    fuchsia::modular::EntityResolver* entity_resolver,
                                    const fidl::StringPtr& entity_reference,
                                    fit::function<void(std::vector<std::string>)> result_call);

}  // namespace modular

#endif  // PERIDOT_BIN_SESSIONMGR_PUPPET_MASTER_COMMAND_RUNNERS_OPERATION_CALLS_GET_TYPES_FROM_ENTITY_CALL_H_
