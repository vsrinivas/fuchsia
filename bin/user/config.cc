// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user/config.h"

namespace maxwell {

std::ostream& operator<<(std::ostream& out, const Config& config) {
  out << "mi_dashboard: " << config.mi_dashboard << std::endl
      << "kronk: " << config.kronk << std::endl
      << "startup_agents:" << std::endl;
  for (const auto& agent : config.startup_agents) {
    out << "  " << agent << std::endl;
  }
  return out;
}

}  // namespace maxwell
