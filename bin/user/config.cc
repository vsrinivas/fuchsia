// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/user/config.h"

namespace maxwell {

std::ostream& operator<<(std::ostream& out, const Config& config) {
  out << "mi_dashboard: " << config.mi_dashboard << std::endl
      << "startup_agents: \n";
  for (const auto& agent : config.startup_agents) {
    out << "  " << agent << std::endl;
  }
  return out;
}

}  // namespace maxwell
