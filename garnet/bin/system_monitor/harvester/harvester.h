// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_SYSTEM_MONITOR_HARVESTER_HARVESTER_H_
#define GARNET_BIN_SYSTEM_MONITOR_HARVESTER_HARVESTER_H_

#include <lib/async/default.h>
#include <lib/zx/time.h>
#include <zircon/types.h>

#include <iostream>
#include <string>

#include "dockyard_proxy.h"
#include "garnet/lib/system_monitor/dockyard/dockyard.h"

class SystemMonitorHarvesterTest;

namespace harvester {

class Harvester {
 public:
  Harvester(zx::duration cycle_msec_rate, zx_handle_t root_resource,
            async_dispatcher_t* dispatcher,
            std::unique_ptr<DockyardProxy> dockyard_proxy);

  void GatherData();

 private:
  zx::duration cycle_period_;
  zx_handle_t root_resource_;
  async_dispatcher_t* dispatcher_;
  std::unique_ptr<harvester::DockyardProxy> dockyard_proxy_;
  friend class ::SystemMonitorHarvesterTest;

  // Gather Samples for a given subject. These are grouped to make the code more
  // manageable and enabling/disabling categories in the future.
  void GatherCpuSamples();
  void GatherMemorySamples();
  void GatherThreadSamples();

  void GatherComponentIntrospection();
};

}  // namespace harvester

#endif  // GARNET_BIN_SYSTEM_MONITOR_HARVESTER_HARVESTER_H_
