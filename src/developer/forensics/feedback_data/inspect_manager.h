// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_INSPECT_MANAGER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_INSPECT_MANAGER_H_

#include <lib/inspect/cpp/vmo/types.h>

#include <cstdint>

#include "src/developer/forensics/utils/inspect_node_manager.h"
#include "src/developer/forensics/utils/inspect_protocol_stats.h"
#include "src/lib/fxl/macros.h"

namespace forensics {
namespace feedback_data {

// Encapsulates the global state exposed through Inspect.
class InspectManager {
 public:
  InspectManager(inspect::Node* root_node);

  // Register creating or closing a connection to ComponentDataRegister.
  void UpdateComponentDataRegisterProtocolStats(InspectProtocolStatsUpdateFn update);

  // Register creating or closing a connection to DataProvider.
  void UpdateDataProviderProtocolStats(InspectProtocolStatsUpdateFn update);

  // Register creating or closing a connection to DeviceIdProvider.
  void UpdateDeviceIdProviderProtocolStats(InspectProtocolStatsUpdateFn update);

 private:
  InspectNodeManager node_manager_;

  InspectProtocolStats component_data_register_stats_;
  InspectProtocolStats data_provider_stats_;
  InspectProtocolStats device_id_provider_stats_;

  FXL_DISALLOW_COPY_AND_ASSIGN(InspectManager);
};

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_INSPECT_MANAGER_H_
