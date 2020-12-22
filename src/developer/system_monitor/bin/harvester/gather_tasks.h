// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_GATHER_TASKS_H_
#define SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_GATHER_TASKS_H_

#include "gather_category.h"
#include "task_tree.h"

namespace harvester {

class SampleBundle;

void AddTaskBasics(SampleBundle* samples,
                   const std::vector<TaskTree::Task>& tasks,
                   dockyard::KoidType type);

void AddProcessStats(SampleBundle* samples,
                     const std::vector<TaskTree::Task>& tasks);

void AddThreadStats(SampleBundle* samples,
                    const std::vector<TaskTree::Task>& tasks);

// Gather Samples for jobs, processes, and threads.
class GatherTasks : public GatherCategory {
 public:
  GatherTasks(zx_handle_t info_resource,
              harvester::DockyardProxy* dockyard_proxy)
      : GatherCategory(info_resource, dockyard_proxy) {}

  // GatherCategory.
  void Gather() override;
};

}  // namespace harvester

#endif  // SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_GATHER_TASKS_H_
