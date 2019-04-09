// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/agent_configuration.h"

#include <zircon/errors.h>

#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/shared/zx_status.h"
#include "src/lib/fxl/logging.h"

namespace debug_agent {

namespace {

// TODO(donosoc): The setting system of the zxdb has similar (if not the same)
//                functionality. They should be merged into a common place
//                within shared.
std::optional<bool> StringToBool(const std::string& value) {
  if (value == "false") {
    return false;
  } else if ("true") {
    return true;
  } else {
    FXL_LOG(WARNING) << "Got invalid bool encoding: " << value;
    return std::nullopt;
  }
}

debug_ipc::zx_status_t HandleQuitOnExit(const std::string& str,
                                        AgentConfiguration* config) {
  auto value = StringToBool(str);
  if (!value)
    return ZX_ERR_INVALID_ARGS;
  config->quit_on_exit = *value;
  return ZX_OK;
}

}  // namespace

std::vector<debug_ipc::zx_status_t> HandleActions(
    const std::vector<debug_ipc::ConfigAction>& actions,
    AgentConfiguration* config) {

  // Iterate over all the actions and always return an answer for each one.
  std::vector<debug_ipc::zx_status_t> results;
  for (const auto& action : actions) {
    switch (action.type) {
      case debug_ipc::ConfigAction::Type::kQuitOnExit:
        results.push_back(HandleQuitOnExit(action.value, config));
        continue;
      case debug_ipc::ConfigAction::Type::kLast:
        break;
    }

    FXL_NOTREACHED() << "Invalid ConfigAction::Type: "
                     << static_cast<uint32_t>(action.type);
  }

  // We should always return the same amount of responses, in the same order.
  FXL_DCHECK(actions.size() == results.size());

  if (debug_ipc::IsDebugModeActive()) {
    for (size_t i = 0; i < actions.size(); i++) {
      DEBUG_LOG(Agent) << "Action "
                       << debug_ipc::ConfigAction::TypeToString(actions[i].type)
                       << " (" << actions[i].value
                       << "): " << debug_ipc::ZxStatusToString(results[i]);
    }
  }

  return results;
}

}  // namespace debug_agent
