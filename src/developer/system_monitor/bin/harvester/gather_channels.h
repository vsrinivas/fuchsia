// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_GATHER_CHANNELS_H_
#define SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_GATHER_CHANNELS_H_

#include "gather_category.h"
#include "task_tree.h"

namespace harvester {

class SampleBundle;

// Gather Samples for jobs, processes, and threads.
class GatherChannels : public GatherCategory {
 public:
  GatherChannels(zx_handle_t info_resource,
                 harvester::DockyardProxy* dockyard_proxy)
      : GatherCategory(info_resource, dockyard_proxy) {}

  // GatherCategory.
  void Gather() override;
};

}  // namespace harvester

#endif  // SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_GATHER_CHANNELS_H_
