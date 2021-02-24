// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_GATHER_PROCESSES_AND_MEMORY_H_
#define SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_GATHER_PROCESSES_AND_MEMORY_H_

#include "gather_category.h"
#include "rate_limiter.h"

namespace harvester {

// Gather samples for process and global memory stats.
class GatherProcessesAndMemory : public GatherCategory {
 public:
  GatherProcessesAndMemory(zx_handle_t info_resource,
                           harvester::DockyardProxy* dockyard_proxy);

  // GatherCategory.
  void Gather() override;

 private:
  RateLimiter limiter_{20};

  GatherProcessesAndMemory() = delete;
};

}  // namespace harvester

#endif  // SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_GATHER_PROCESSES_AND_MEMORY_H_
