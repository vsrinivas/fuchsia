// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <list>
#include <string>

namespace maxwell {

struct Config {
  // A list of Agents to start during Maxwell initialization.
  std::list<std::string> startup_agents;

  // Set to true if the MI Dashboard should be started.
  bool mi_dashboard;
  // Set to true if the Kronk agent should be started.
  bool kronk;
};

}  // namespace maxwell
