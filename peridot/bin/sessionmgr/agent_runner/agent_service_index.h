// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SESSIONMGR_AGENT_RUNNER_AGENT_SERVICE_INDEX_H_
#define PERIDOT_BIN_SESSIONMGR_AGENT_RUNNER_AGENT_SERVICE_INDEX_H_

#include <lib/fit/optional.h>

#include <string>

namespace modular {

// Provides an interface to find an agent that can provide a requested service,
// by name.
class AgentServiceIndex {
 public:
  virtual ~AgentServiceIndex() = default;

  // Returns the component URL (handler) of an agent that can provide the
  // |service_name|. If not found, the fit::optional has no value.
  //
  // |AgentServiceIndex| does not perform any validation that |service_name|
  // will be provided to all clients who request it of the returned handler.
  virtual fit::optional<std::string> FindAgentForService(
      std::string service_name) = 0;
};

}  // namespace modular

#endif  // PERIDOT_BIN_SESSIONMGR_AGENT_RUNNER_AGENT_SERVICE_INDEX_H_
