// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_SYSTEM_MONITOR_HARVESTER_GATHER_TASKS_H_
#define GARNET_BIN_SYSTEM_MONITOR_HARVESTER_GATHER_TASKS_H_

#include "gather_category.h"

namespace harvester {

// Gather Samples for jobs, processes, and threads.
class GatherTasks : public GatherCategory {
 public:
  GatherTasks(zx_handle_t root_resource,
              harvester::DockyardProxy& dockyard_proxy)
      : GatherCategory(root_resource, dockyard_proxy) {}

  void Gather() override;
};

}  // namespace harvester

#endif  // GARNET_BIN_SYSTEM_MONITOR_HARVESTER_GATHER_TASKS_H_
