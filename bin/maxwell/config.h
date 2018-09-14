// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_MAXWELL_CONFIG_H_
#define PERIDOT_BIN_MAXWELL_CONFIG_H_

#include <list>
#include <ostream>
#include <string>

namespace maxwell {

struct Config {
  // A list of Agents to start during Maxwell initialization.
  std::list<std::string> startup_agents;

  // A list of Agents that get session capabilities such as Puppet master and
  // are started during Maxwell initialization.
  std::list<std::string> session_agents;

  // Set to true if the MI Dashboard should be started.
  bool mi_dashboard = false;
};

std::ostream& operator<<(std::ostream& out, const Config& config);

}  // namespace maxwell

#endif  // PERIDOT_BIN_MAXWELL_CONFIG_H_
