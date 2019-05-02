// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SESSIONMGR_AGENT_RUNNER_MAP_AGENT_SERVICE_INDEX_H_
#define PERIDOT_BIN_SESSIONMGR_AGENT_RUNNER_MAP_AGENT_SERVICE_INDEX_H_

#include <src/lib/fxl/macros.h>

#include <map>

#include "peridot/bin/sessionmgr/agent_runner/agent_service_index.h"

namespace modular {

// Mantains a mapping from named services to the agents that offer the service.
class MapAgentServiceIndex : public AgentServiceIndex {
 public:
  // Construct an index with the given map from a unique |service_name| key
  // to the |agent_url| that provides the service. If more than one agent
  // exists for a given service, only one of them can be registered with this
  // |MapAgentServiceIndex| implementation. (Since FindAgentForService() only
  // returns one |agent_url| for a given |service_name|, this restriction simply
  // means the given map predefines which |agent_url| is returned.)
  MapAgentServiceIndex(std::map<std::string, std::string> service_to_agents)
      : service_to_agents_(service_to_agents) {}

  ~MapAgentServiceIndex() = default;

  // Implementation for |AgentServiceIndex|.
  fit::optional<std::string> FindAgentForService(
      std::string service_name) override {
    auto it = service_to_agents_.find(service_name);
    if (it == service_to_agents_.end()) {
      return fit::nullopt;
    }
    return it->second;
  }

  FXL_DISALLOW_COPY_AND_ASSIGN(MapAgentServiceIndex);

 private:
  std::map<std::string, std::string> service_to_agents_;
};

}  // namespace modular

#endif  // PERIDOT_BIN_SESSIONMGR_AGENT_RUNNER_MAP_AGENT_SERVICE_INDEX_H_
