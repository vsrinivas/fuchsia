// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_INSPECT_MANAGER_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_INSPECT_MANAGER_H_

#include <lib/inspect/cpp/vmo/types.h>

#include <cstdint>

#include "src/developer/feedback/utils/inspect_node_manager.h"
#include "src/lib/fxl/macros.h"

namespace feedback {

// Encapsulates the global state exposed through Inspect.
class InspectManager {
 public:
  InspectManager(inspect::Node* root_node);

  // Increments the current and total numbers of DataProvider connections.
  void IncrementNumDataProviderConnections();
  // Decrements the current number of DataProvider connections.
  void DecrementCurrentNumDataProviderConnections();

 private:
  // Inspect node containing the DataProvider stats.
  struct DataProviderStats {
    inspect::UintProperty total_num_connections;
    inspect::UintProperty current_num_connections;
  };

  InspectNodeManager node_manager_;

  DataProviderStats data_provider_stats_;

  FXL_DISALLOW_COPY_AND_ASSIGN(InspectManager);
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_INSPECT_MANAGER_H_
