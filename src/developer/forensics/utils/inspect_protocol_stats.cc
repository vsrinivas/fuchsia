// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/utils/inspect_protocol_stats.h"

namespace forensics {

InspectProtocolStats::InspectProtocolStats(InspectNodeManager* node, const std::string& path) {
  current_num_connections_ = node->Get(path).CreateUint("current_num_connections", 0);
  total_num_connections_ = node->Get(path).CreateUint("total_num_connections", 0);
}

void InspectProtocolStats::NewConnection() {
  current_num_connections_.Add(1);
  total_num_connections_.Add(1);
}

void InspectProtocolStats::CloseConnection() { current_num_connections_.Subtract(1); }

}  // namespace forensics
