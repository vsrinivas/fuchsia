// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_UTILS_INSPECT_PROTOCOL_STATS_H_
#define SRC_DEVELOPER_FORENSICS_UTILS_INSPECT_PROTOCOL_STATS_H_

#include <lib/inspect/cpp/vmo/types.h>

#include "src/developer/forensics/utils/inspect_node_manager.h"
#include "src/lib/fxl/macros.h"

namespace forensics {

// Inspect node containing stats for a given protocol.
class InspectProtocolStats {
 public:
  InspectProtocolStats(InspectNodeManager* node, const std::string& path);
  void NewConnection();
  void CloseConnection();

 private:
  // Current number of active connections.
  inspect::UintProperty current_num_connections_;
  // Total number of connections ever created, active and closed.
  inspect::UintProperty total_num_connections_;

  FXL_DISALLOW_COPY_AND_ASSIGN(InspectProtocolStats);
};

// InspectProtocolStats member function alias.
// Clients can use this alias for the type of &InspectProtocolStats::NewConnection or
// &InspectProtocolStats::CloseConnection.
using InspectProtocolStatsUpdateFn = void (InspectProtocolStats::*)();

}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_UTILS_INSPECT_PROTOCOL_STATS_H_
