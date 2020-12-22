// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_HARVESTER_H_
#define SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_HARVESTER_H_

#include "dockyard_proxy.h"
#include "gather_channels.h"
#include "gather_cpu.h"
#include "gather_device_info.h"
#include "gather_inspectable.h"
#include "gather_introspection.h"
#include "gather_memory.h"
#include "gather_memory_digest.h"
#include "gather_processes_and_memory.h"
#include "gather_tasks.h"
#include "gather_threads_and_cpu.h"
#include "gather_vmos.h"
#include "log_listener.h"
#include "os.h"
#include "src/developer/system_monitor/lib/dockyard/dockyard.h"

class SystemMonitorHarvesterTest;

namespace harvester {

// The Harvester manages the various gathering code. Separate members gather
// different types of Dockyard Samples as directed by the Harvester.
class Harvester {
 public:
  Harvester(zx_handle_t info_resource,
            std::unique_ptr<DockyardProxy> dockyard_proxy,
            std::unique_ptr<OS> os);

  // Gather one-time data that doesn't vary over time. E.g. total RAM.
  void GatherDeviceProperties();

  // Gather structured logs from ArchiveAccessor
  void GatherLogs();

  // Gather a snapshot of data that may vary over time. E.g. used RAM.
  void GatherFastData(async_dispatcher_t* dispatcher);
  void GatherSlowData(async_dispatcher_t* dispatcher);

 private:
  zx_handle_t info_resource_;
  std::unique_ptr<harvester::DockyardProxy> dockyard_proxy_;
  std::unique_ptr<harvester::OS> os_;
  LogListener log_listener_;

  GatherChannels gather_channels_{info_resource_, dockyard_proxy_.get()};
  GatherCpu gather_cpu_{info_resource_, dockyard_proxy_.get()};
  GatherDeviceInfo gather_device_info_{info_resource_, dockyard_proxy_.get()};
  GatherInspectable gather_inspectable_{info_resource_, dockyard_proxy_.get()};
  GatherIntrospection gather_introspection_{info_resource_,
                                            dockyard_proxy_.get()};
  GatherMemory gather_memory_{info_resource_, dockyard_proxy_.get()};
  GatherMemoryDigest gather_memory_digest_{info_resource_,
                                           dockyard_proxy_.get()};
  GatherTasks gather_tasks_{info_resource_, dockyard_proxy_.get()};
  GatherThreadsAndCpu gather_threads_and_cpu_{info_resource_,
                                              dockyard_proxy_.get()};
  GatherProcessesAndMemory gather_processes_and_memory_{info_resource_,
                                                        dockyard_proxy_.get()};
  GatherVmos gather_vmos_{info_resource_, dockyard_proxy_.get(),
                          g_slow_data_task_tree, os_.get()};

  friend class ::SystemMonitorHarvesterTest;
};

}  // namespace harvester

#endif  // SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_HARVESTER_H_
