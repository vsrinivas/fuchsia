// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/inspect_manager.h"

namespace forensics {
namespace feedback_data {

InspectManager::InspectManager(inspect::Node* root_node)
    : node_manager_(root_node),
      component_data_register_stats_(&node_manager_,
                                     "/fidl/fuchsia.feedback.ComponentDataRegister"),
      data_provider_stats_(&node_manager_, "/fidl/fuchsia.feedback.DataProvider"),
      device_id_provider_stats_(&node_manager_, "/fidl/fuchsia.feedback.DeviceIdProvider") {}

void InspectManager::UpdateComponentDataRegisterProtocolStats(InspectProtocolStatsUpdateFn update) {
  std::invoke(update, component_data_register_stats_);
}

void InspectManager::UpdateDataProviderProtocolStats(InspectProtocolStatsUpdateFn update) {
  std::invoke(update, data_provider_stats_);
}

void InspectManager::UpdateDeviceIdProviderProtocolStats(InspectProtocolStatsUpdateFn update) {
  std::invoke(update, device_id_provider_stats_);
}

}  // namespace feedback_data
}  // namespace forensics
