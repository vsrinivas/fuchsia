// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_SESSIONMGR_AGENT_SERVICES_FACTORY_H_
#define SRC_MODULAR_BIN_SESSIONMGR_AGENT_SERVICES_FACTORY_H_

#include <fuchsia/sys/cpp/fidl.h>

#include <string>
#include <vector>

namespace modular {

class AgentServicesFactory {
 public:
  virtual ~AgentServicesFactory() = default;

  virtual fuchsia::sys::ServiceList GetServicesForAgent(std::string agent_url) = 0;
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_SESSIONMGR_AGENT_SERVICES_FACTORY_H_
