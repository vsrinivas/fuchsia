// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_SYSTEM_MONITOR_HARVESTER_GATHER_CATEGORY_H_
#define GARNET_BIN_SYSTEM_MONITOR_HARVESTER_GATHER_CATEGORY_H_

#include <lib/async/cpp/task.h>
#include <lib/async/cpp/time.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/default.h>

#include <string>

#include "src/developer/system_monitor/lib/dockyard/dockyard.h"

class SystemMonitorHarvesterTest;

namespace harvester {

class DockyardProxy;

// Utility for error output.
std::string ZxErrorString(const std::string& cmd, zx_status_t err);

// Gather Samples for a given subject. These are grouped to make the code more
// manageable and enabling/disabling categories.
class GatherCategory {
 public:
  GatherCategory(zx_handle_t root_resource,
                 harvester::DockyardProxy* dockyard_proxy)
      : root_resource_(root_resource), dockyard_proxy_(dockyard_proxy) {}
  virtual ~GatherCategory() = default;

  // The dockyard proxy is used to send data to the remote Dockyard.
  harvester::DockyardProxy& Dockyard() { return *dockyard_proxy_; }
  harvester::DockyardProxy* DockyardPtr() { return dockyard_proxy_; }

  // Gather one-time data that doesn't vary over time. E.g. total RAM.
  virtual void GatherDeviceProperties(){};

  // Override this in a base class to gather sample data.
  virtual void Gather() = 0;

  // Get the root resource of the job/process/thread tree.
  zx_handle_t RootResource() { return root_resource_; }

  // Set (or reset) the time this task will run on |dispatcher|.
  // |Gather()| will be called at (or after) |start| and then every multiple of
  // |period|.
  void PostUpdate(async_dispatcher_t* dispatcher, zx::time start,
                  zx::duration period);

  // For use by the task dispatcher.
  void TaskHandler(async_dispatcher_t* dispatcher, async::TaskBase* task,
                   zx_status_t status);

 private:
  async::TaskMethod<GatherCategory, &GatherCategory::TaskHandler> task_method_{
      this};
  zx_handle_t root_resource_;
  harvester::DockyardProxy* dockyard_proxy_;

  zx::duration update_period_;
  zx::time next_update_;

  friend class ::SystemMonitorHarvesterTest;
};

}  // namespace harvester

#endif  // GARNET_BIN_SYSTEM_MONITOR_HARVESTER_GATHER_CATEGORY_H_
