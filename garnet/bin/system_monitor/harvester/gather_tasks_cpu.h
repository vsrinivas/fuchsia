// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_SYSTEM_MONITOR_HARVESTER_GATHER_TASKS_CPU_H_
#define GARNET_BIN_SYSTEM_MONITOR_HARVESTER_GATHER_TASKS_CPU_H_

#include "gather_category.h"

namespace harvester {

class TaskTree;

// Determine which actions to take at each interval.
class TaskActions {
 public:
  TaskActions() = default;

  // Asking which actions to take.
  bool WantRefresh() { return !(counter_ % REFRESH_INTERVAL); }
  bool WantThreadCpuSamples() { return true; }

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

// Gather Samples for jobs, processes, and threads.
class GatherTasksCpu : public GatherCategory {
 public:
  GatherTasksCpu(zx_handle_t root_resource,
                 harvester::DockyardProxy* dockyard_proxy);

  ~GatherTasksCpu() override;

  // GatherCategory.
  void Gather() override;

 private:
  TaskActions actions_;
  TaskTree* task_tree_;
};

}  // namespace harvester

#endif  // GARNET_BIN_SYSTEM_MONITOR_HARVESTER_GATHER_TASKS_CPU_H_
