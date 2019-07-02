// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_SYSTEM_MONITOR_HARVESTER_HARVESTER_H_
#define GARNET_BIN_SYSTEM_MONITOR_HARVESTER_HARVESTER_H_

#include "dockyard_proxy.h"
#include "garnet/lib/system_monitor/dockyard/dockyard.h"
#include "gather_cpu.h"
#include "gather_inspectable.h"
#include "gather_introspection.h"
#include "gather_memory.h"
#include "gather_tasks.h"

class SystemMonitorHarvesterTest;

namespace harvester {

// The Harvester manages the various gathering code. Separate members gather
// different types of Dockyard Samples as directed by the Harvester.
class Harvester {
 public:
  Harvester(zx_handle_t root_resource, async_dispatcher_t* dispatcher,
            std::unique_ptr<DockyardProxy> dockyard_proxy);

  void GatherData();

 private:
  zx_handle_t root_resource_;
  async_dispatcher_t* dispatcher_;
  std::unique_ptr<harvester::DockyardProxy> dockyard_proxy_;

  GatherCpu gather_cpu_{root_resource_, *dockyard_proxy_};
  GatherInspectable gather_inspectable_{root_resource_, *dockyard_proxy_};
  GatherIntrospection gather_introspection_{root_resource_, *dockyard_proxy_};
  GatherMemory gather_memory_{root_resource_, *dockyard_proxy_};
  GatherTasks gather_tasks_{root_resource_, *dockyard_proxy_};

  friend class ::SystemMonitorHarvesterTest;
};

}  // namespace harvester

#endif  // GARNET_BIN_SYSTEM_MONITOR_HARVESTER_HARVESTER_H_
