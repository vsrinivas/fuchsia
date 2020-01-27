// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_SYSTEM_MONITOR_HARVESTER_HARVESTER_FAKE_H_
#define GARNET_BIN_SYSTEM_MONITOR_HARVESTER_HARVESTER_FAKE_H_

#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/default.h>
#include <lib/zx/time.h>
#include <zircon/types.h>

#include <iostream>
#include <string>

#include "dockyard_proxy.h"
#include "harvester.h"
#include "src/developer/system_monitor/lib/dockyard/dockyard.h"

namespace harvester {

class Harvester;

class HarvesterFake : public Harvester {
 public:
  HarvesterFake(zx_handle_t root_resource,
                std::unique_ptr<DockyardProxy> dockyard_proxy)
      : Harvester(root_resource, /*dispatcher=*/nullptr,
                  std::move(dockyard_proxy)) {}

  void GatherData() {}

  void SetUpdatePeriod(dockyard::DockyardId dockyard_id,
                       zx::duration update_period);
};

}  // namespace harvester

#endif  // GARNET_BIN_SYSTEM_MONITOR_HARVESTER_HARVESTER_FAKE_H_
