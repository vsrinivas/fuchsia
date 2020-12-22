// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_GATHER_THREADS_AND_CPU_H_
#define SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_GATHER_THREADS_AND_CPU_H_

#include "gather_category.h"

namespace harvester {

// Determine which actions to take at each interval.
class TaskActions {
 public:
  TaskActions() = default;

  // Asking which actions to take.
  bool WantRefresh() { return !(counter_ % REFRESH_INTERVAL); }

  // Call this at the end of each interval.
  void NextInterval() { ++counter_; }

 private:
  // Only gather and upload this data every Nth time this is called. Reuse the
  // same task info for the other (N - 1) times. This is an optimization. If
  // the overhead/time to gather this information is reduced then this
  // optimization may be removed.
  static const int REFRESH_INTERVAL = {20};
  int counter_ = 0;
};

// Gather samples for threads and global CPU stats.
class GatherThreadsAndCpu : public GatherCategory {
 public:
  GatherThreadsAndCpu(zx_handle_t info_resource,
                      harvester::DockyardProxy* dockyard_proxy);

  // GatherCategory.
  void Gather() override;

 private:
  TaskActions actions_;

  GatherThreadsAndCpu() = delete;
};

}  // namespace harvester

#endif  // SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_GATHER_THREADS_AND_CPU_H_
