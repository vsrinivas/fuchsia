// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/inspect_manager.h"

namespace feedback {

InspectManager::InspectManager(inspect::Node* root_node) : node_manager_(root_node) {
  node_manager_.Get("/data_provider");
  data_provider_stats_.current_num_connections =
      node_manager_.Get("/data_provider").CreateUint("current_num_connections", 0);
  data_provider_stats_.total_num_connections =
      node_manager_.Get("/data_provider").CreateUint("total_num_connections", 0);
}

void InspectManager::IncrementNumDataProviderConnections() {
  data_provider_stats_.current_num_connections.Add(1);
  data_provider_stats_.total_num_connections.Add(1);
}

void InspectManager::DecrementCurrentNumDataProviderConnections() {
  data_provider_stats_.current_num_connections.Subtract(1);
}

}  // namespace feedback
