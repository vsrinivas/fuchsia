// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/inspect_manager.h"

namespace feedback {

InspectManager::InspectManager(inspect::Node* root_node) : node_manager_(root_node) {
  node_manager_.Get("/fidl/fuchsia.feedback.ComponentDataRegister");
  component_data_register_stats_.current_num_connections =
      node_manager_.Get("/fidl/fuchsia.feedback.ComponentDataRegister")
          .CreateUint("current_num_connections", 0);
  component_data_register_stats_.total_num_connections =
      node_manager_.Get("/fidl/fuchsia.feedback.ComponentDataRegister")
          .CreateUint("total_num_connections", 0);

  node_manager_.Get("/fidl/fuchsia.feedback.DataProvider");
  data_provider_stats_.current_num_connections =
      node_manager_.Get("/fidl/fuchsia.feedback.DataProvider")
          .CreateUint("current_num_connections", 0);
  data_provider_stats_.total_num_connections =
      node_manager_.Get("/fidl/fuchsia.feedback.DataProvider")
          .CreateUint("total_num_connections", 0);

  node_manager_.Get("/fidl/fuchsia.feedback.DeviceIdProvider");
  device_id_provider_stats_.current_num_connections =
      node_manager_.Get("/fidl/fuchsia.feedback.DeviceIdProvider")
          .CreateUint("current_num_connections", 0);
  device_id_provider_stats_.total_num_connections =
      node_manager_.Get("/fidl/fuchsia.feedback.DeviceIdProvider")
          .CreateUint("total_num_connections", 0);
}

void InspectManager::IncrementNumComponentDataRegisterConnections() {
  component_data_register_stats_.current_num_connections.Add(1);
  component_data_register_stats_.total_num_connections.Add(1);
}

void InspectManager::DecrementCurrentNumComponentDataRegisterConnections() {
  component_data_register_stats_.current_num_connections.Subtract(1);
}

void InspectManager::IncrementNumDataProviderConnections() {
  data_provider_stats_.current_num_connections.Add(1);
  data_provider_stats_.total_num_connections.Add(1);
}

void InspectManager::DecrementCurrentNumDataProviderConnections() {
  data_provider_stats_.current_num_connections.Subtract(1);
}

void InspectManager::IncrementNumDeviceIdProviderConnections() {
  device_id_provider_stats_.current_num_connections.Add(1);
  device_id_provider_stats_.total_num_connections.Add(1);
}

void InspectManager::DecrementCurrentNumDeviceIdProviderConnections() {
  device_id_provider_stats_.current_num_connections.Subtract(1);
}

}  // namespace feedback
